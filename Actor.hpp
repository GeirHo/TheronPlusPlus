/*=============================================================================
Actor

This is a minimalistic implementation of the actor system aiming to rectify 
some of the issues with Theron and basing it strictly on modern C++ principles.
The design goals are

1. Do not redo what C++ has as standard: This includes using the standard 
   memory allocation mechanism. This should also ensure full portability.
   The compiler should be able to handle the magic.
2. Simplicity. The main consequence of this is that the actors are supposed 
   to run on one computer with shared memory
3. Respect the Theron interface. One should be able to replace the Theron main
   header with this and recompile. For this reason, there is no dedicated 
   documentation of the API since one can simply read Theron's documentation.

Currently there is no scheduler. The operating system's thread scheduler is 
used. Each actor has its own thread for executing its message handlers. This is
based on the assumption that the system has sufficient memory, and that a 
thread not running is not consuming CPU. The actual scheduling of active threads
on the CPU cores should be most efficient in this way. However, one should 
measure, and if the execution environment cannot cope with this policy a 
thread pool and a scheduling policy can be implemented, although it currently 
contradicts the second design principle.

Author and Copyright: Geir Horn, 2017
License: LGPL 3.0
=============================================================================*/

#ifndef THERON_REPLACEMENT_ACTOR
#define THERON_REPLACEMENT_ACTOR

#include <string>							// Strings
#include <memory>							// Smart pointers
#include <queue>							// For the queue of messages
#include <mutex>						  // To protect the queue
#include <unordered_map>			// To map actor names to actors
#include <set>							  // To keep track of actor addresses
#include <functional>					// For defining handler functions
#include <thread>						  // To execute actors
#include <stdexcept>				  // To throw standard exceptions
#include <sstream>						// To provide nice exception messages


namespace Theron {
class Actor
{
/*=============================================================================

 Actor identification (alias Framework)

=============================================================================*/

public:

// The actor identification will store references to addresses referring to this
// actor, and therefore the address class must be forward declared.
	
class Address;

private: 

// An actor has a name and a numerical ID. The latter must be unique independent
// of how many actors that are created and destroyed over the lifetime of the 
// application. A special object is charged with obtaining the numerical ID and 
// store the name and the registration of the actor during the actor's lifetime

class ActorIdentification
{
public:
	
	// The Numerical ID of an actor must be long enough to hold the counter of all 
	// actors created during the application's lifetime. 
	
	using IDType = unsigned long;
	
private:
	
	// It maintains a global counter which should be long enough to ensure that 
	// there are enough IDs for actors. This is incremented by each actor to 
	// avoid that any actor ever gets the same ID
	
	static IDType TotalActorsCreated;
	
	// The main address of an actor is its physical memory location. It may be 
	// necessary to look up actors based on their name, and the best way
	// to do this is using an unordered map since a lookup should be O(1). 

	static std::unordered_map< std::string, Actor * > ActorsByName;
	
	// In the same way there is a lookup map to find the pointer based on the 
	// actor's ID. This could have been a vector, but it would have been ever 
	// growing. Having a map allows the storage of only active actors.
	
	static std::unordered_map< IDType, Actor * > ActorsByID;
	
	// Since these three elements are shared among all actors, and actors can 
	// be created by other actors in the message handlers or otherwise, the
	// elements can be operated upon from different threads. It is therefore 
	// necessary to ensure sequential access by locking a mutex.
	
	static std::mutex InformationAccess;
	
	// There is a small function to increment the total number of actors and 
	// return that id
	
	inline static IDType GetNewID( void )
	{
		std::lock_guard< std::mutex > Lock( InformationAccess );
		return ++TotalActorsCreated;
	}
	
	// The pointer to this actor is stored locally for quick reference.
	
	Actor * ActorPointer; 
	
public:	
	
	// Other actors may need to obtain a pointer to this actor, and this can 
	// only be done through a legal address object. It is not declared in-line 
	// since it uses the internals of the actor definition. This function will 
	// throw invalid_argument if the address is not for a valid and running actor.
	
	static Actor * GetActor( Address & ActorAddress );
		
	// It also maintains the actor name and the actual ID assigned to this 
	// actor. Note that these have to be constant for the full duration of the 
	// actor's lifetime

	const IDType NumericalID;
	const std::string Name;

	// The second part of the identification management is to keep track of 
	// external addresses referring to this actor. One potential issue with 
	// Theron is that an address can outlive the object it is an implicit 
	// reference to, and therefore an actor can send a message to an actor that 
	// is no longer existing, and there is no exception mechanism to handle this
	// error situation. To remedy this situation there is a set of references to
	// addresses obtained for this actor, and they will all be invalidated by 
	// the destructor.
	
private:
	
	std::set< Address * > Addresses;
	
	// And there are two functions to register and de-register an address object
	
public:
	
	inline void Register( Address * NewAddress )
	{
		Addresses.insert( NewAddress );
	}
	
	inline void DeRegister( Address * OldAddress )
	{
		Addresses.erase( OldAddress );
	}
	
	// There are static functions to obtain an address by name or by numerical 
	// ID. They are not defined in-line since they return an address class which 
	// is not yet fully specified.
	
	static Address Lookup( const std::string ActorName );
	
	static Address Lookup( const IDType TheID );

	// Constructor and destructor
	//	
	// In order to fully register the actor, a pointer to the actor is needed.
	// However, the actor identification object is supposed to be an element of 
	// the actor and hence it should be constructed prior to running the actor's
	// constructor. The "this" pointer will exist for the actor's memory area 
	// and it should be possible to store it, however some compilers may rightly 
	// give a warning. If no name is given to the actor, it will be given the 
	// name "ActorNN" where NN is the numerical ID of the actor.

	ActorIdentification( Actor * TheActor, 
											 const std::string & ActorName = std::string() )
	: ActorPointer( TheActor ), NumericalID( GetNewID() ),
	  Name( ActorName.empty() ? 
				  "Actor" + std::to_string( NumericalID ) : ActorName ),
	  Addresses()
	{
		std::lock_guard< std::mutex > Lock( InformationAccess );
		
		auto Outcome = ActorsByName.emplace( Name, TheActor );
		
		if ( Outcome.second != true )
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << "An actor with the name " << Name 
									 << " does already exist!";
			
			throw std::invalid_argument( ErrorMessage.str() );
		}
		
		ActorsByID.emplace( NumericalID, TheActor );
	}

	// The destructor method cannot be defined in-line since it will call 
	// methods on the addresses to invalidate them and the address class is 
	// not yet fully specified.
	
	~ActorIdentification( void );
	
} ActorID;
	
		
/*=============================================================================

 Address management

=============================================================================*/

// The main address of an actor is it physical memory location. This can be 
// reached only through an address object that has a pointer to the actor 
// ID of the relevant actor. It also provides a method to invalidate this 
// pointer when the actor closes.

public: 
	
class Address
{
private:
	
	ActorIdentification * TheActor;
	
	// The address can be invalidated only by the actor identification class 
	// when the actor is destructed.
	
	void Invalidate( void )
	{
		if ( TheActor != nullptr )
		{
			TheActor->DeRegister( this );
			TheActor = nullptr;
		}
	}
	
	// and the Actor Identification class is a friend that is allowed to use 
	// this method.
	
	friend class ActorIdentification;
	
public:
	
	// The constructor takes the pointer to the actor identification class of 
	// the actor this address refers to, and then register with this actor 
	// if the pointer given is not Null.
	
	inline Address( ActorIdentification * ReferencedActor = nullptr )
	: TheActor( ReferencedActor )
	{ 
		if ( TheActor != nullptr )
			TheActor->Register( this );
	}

	// There is a copy constructor that defers the construction to the normal 
	// constructor, which implies that the copied address is also registered with
	// the actor.
	
	inline Address( const Address & OtherAddress )
	: Address( OtherAddress.TheActor )
	{	}
	
	// The move constructor is more elaborate as it will invalidate the object 
	// moved.
	
	inline Address( Address && OtherAddress )
	: Address( OtherAddress.TheActor )
	{
		OtherAddress.Invalidate();
	}
	
  // There is constructor to find an address by name and it is suspected that 
	// this will be used also for the plain string defined by Theron 
	// (const char *const name). These will simply use the Actor identification's 
	// lookup and then delegate to the above constructors.
	
	inline Address( const std::string & Name )
	: Address( ActorIdentification::Lookup( Name ) )
	{	}
	
	// Oddly enough, but Theron has no constructor to get an address by the 
	// numerical ID of the actor, so a similar one is provided now.
	
	inline Address( const ActorIdentification::IDType & ID )
	: Address( ActorIdentification::Lookup( ID ) )
	{	}	

	// It should have a static function Null to allow test addresses
	
	static Address Null( void )
	{
		return Address();
	}
	
	// There is an implicit conversion to a boolean to check if the address is 
	// valid or not.
	
	operator bool (void )
	{
		return TheActor != nullptr;
	}
	
	// Then it must provide access to the actor's name and ID that can be 
	// taken from the actor's stored information.
	
	inline std::string AsString( void ) const
	{
		return TheActor->Name;
	}
	
	inline ActorIdentification::IDType AsInteger( void ) const
	{
		return TheActor->NumericalID;
	}
	
	inline ActorIdentification::IDType AsUInt64( void ) const
	{
		return AsInteger();
	}
	
	// The destructor simply invalidates the object
	
	inline ~Address( void )
	{
		Invalidate();
	}
};

// The standard way of obtaining the address of an actor will then just return 
// an address class constructed on the basis of its ID. The original 
// Theron version of this is constant qualified, but it defeats the purpose of 
// being able to access and potentially change the actor's state through this
// address pointer.

inline Address GetAddress( void )
{
	return Address( &ActorID );
}

/*=============================================================================

 Messages

=============================================================================*/

// One of the main reasons for doing this re-design is the message handling and 
// its link to a complicated memory management and the possibility not to use
// the Run-time type information (RTTI). Understandably, this may be needed for
// embedded applications but it makes it hard to follow the message flow, and 
// it is error prone. The current effort is motivated by the fact that null
// pointer messages happened frequently in a larger actor system bringing down
// the whole application and basically making Theron useless. 
//
// The approach taken here is simply that each actor has its own message queue
// and that the send operation on one actor simply inserts the message into 
// the message queue of the receiving actor. Thus, the message is created once, 
// and deleted when it has been consumed. C++ and RTTI will have to take care 
// of the polymorphic messages.

private:
	
// -----------------------------------------------------------------------------
// Messages 
// -----------------------------------------------------------------------------
//
// The message stores the address of the sending actor, and has a method to 
// be used by the queue handler when checking if it can be forwarded to the 
// registered message handlers.
	
class GenericMessage
{
public:
	
	const Address From;
	
	GenericMessage( const Address & Sender )
	: From( Sender )
	{ }
};

// The type specific message will define the function to process the message 
// by first trying to cast the handler to the handler templated for the 
// actual message type, and if successful, it will invoke the handler function.

template< class MessageType >
class Message : public GenericMessage
{
public:
	
	const std::shared_ptr< MessageType > TheMessage;

	Message( std::shared_ptr< MessageType > & MessageCopy, const Address & From )
	: GenericMessage( From ), TheMessage( MessageCopy )
	{ }
	
	virtual ~Message( void )
	{ }
};

// Each actor has a queue of messages and add new messages to the end and 
// consume from the front of the queue. It can therefore be implemented as a 
// standard queue.

std::queue< std::shared_ptr< GenericMessage > > IncomingMessages;

// Since this queue will be written to by the sending actors and processed 
// by this actor, there may be several threads trying to operate on the queue 
// at the same time, so sequential access must be ensured by a mutex.

std::mutex QueueGuard;

// Messages are queued a dedicated function that obtains a unique lock on the 
// queue guard before adding the message.

void EnqueueMessage( std::shared_ptr< GenericMessage > & TheMessage );

// -----------------------------------------------------------------------------
// Handlers 
// -----------------------------------------------------------------------------
//
// The Generic Message class is only supposed to be a polymorphic place holder
// for the type specific messages constructed by the send function. It provides
// an interface for a generic message handler, and will call this if its pointer
// can be cast into the type specific handler class (see below). 

class GenericHandler
{
public: 
	
	virtual bool ProcessMessage( 
													std::shared_ptr< GenericMessage > & TheMessage ) = 0;
	
	virtual ~GenericHandler( void )
	{ }
};

// The actual type specific handler is a template on the message type executing 
// the handler function

template< class MessageType >
class Handler : public GenericHandler
{
private:
	
	// The handler must store the function to process the message. This has the 
	// same signature as the actual handler, and it is specified to call the 
	// function on the actor having this handler.
	
	std::function< void( const MessageType &, const Address ) > HandlerFunction;
	
public:
	
	virtual bool ProcessMessage( std::shared_ptr< GenericMessage > & TheMessage )
	{
		std::shared_ptr< Message< MessageType > > TypedMessage = 
							std::dynamic_pointer_cast< Message< MessageType > >( TheMessage );
		
		if ( TypedMessage )
		{
			HandlerFunction( *(TypedMessage->TheMessage), TypedMessage->From );
			return true;
		}
		else 
			return false;
	}
	
	// The constructor stores the handler function.
	
	Handler( const std::function< void( const MessageType &, const Address ) > 
						& GivenHandler )
	: GenericHandler(), HandlerFunction( GivenHandler )
	{ }
	
	virtual ~Handler( void )
	{ }
};

};			// Class Actor
}				// Name space Theron
#endif  // THERON_REPLACEMENT_ACTOR
