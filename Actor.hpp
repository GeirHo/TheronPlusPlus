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

TODO:The current implementation is not able to intercept messages to actors on 
remote nodes, and more thinking is needed on how to do this in a consistent way
to ensure complete transparency for the involved actors. Including a "To" field
in the message would allow it to be intercepted if there is no local actor,
but the address is now linked to active local actors. Sending by name raises 
a secondary issue on how to distinguish between endpoint external actors and 
actors that have closed.

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
#include <atomic>							// Thread protected variables
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
//	
// There are fundamentally two types of actors: Those running on this network 
// endpoint, and those on remote endpoints. Those on remote endpoints are 
// identified only by their actor name. Hence, there is a basic identification
// class holding the name and its numerical ID. 
	
class Identification
{
public:
	
	// The Numerical ID of an actor must be long enough to hold the counter of all 
	// actors created during the application's lifetime. 
	
	using IDType = unsigned long;

		// It also maintains the actor name and the actual ID assigned to this 
	// actor. Note that these have to be constant for the full duration of the 
	// actor's lifetime

	const IDType NumericalID;
	const std::string Name;
	
private:

	// It maintains a global counter which should be long enough to ensure that 
	// there are enough IDs for actors. This is incremented by each actor to 
	// avoid that any actor ever gets the same ID
	
	static std::atomic< IDType > TotalActorsCreated;
	
	// There is a small function to increment the total number of actors and 
	// return that id
	
	inline static IDType GetNewID( void )
	{
		return ++TotalActorsCreated;
	}
	
	// There is a pointer to the actor for which this is the ID. This 
	// will be the real actor on this endpoint, and for actors on remote 
	// endpoints, it will be the Session Layer actor which is responsible for 
	// the serialisation and forwarding of the message to the remote actor. 

	Actor * ActorPointer; 

	// It will also keep track of the known actors, both by name and by ID.
	// All actors must have an identification, locally or remotely. It may be 
	// necessary to look up actors based on their name, and the best way
	// to do this is using an unordered map since a lookup should be O(1). 
	// These maps are protected since it is the responsibility of the derived
	// identity classes to ensure that the name and ID is correctly stored.

protected:
	
	static std::unordered_map< std::string, Identification * > ActorsByName;
	
	// In the same way there is a lookup map to find the pointer based on the 
	// actor's ID. This could have been a vector, but it would have been ever 
	// growing. Having a map allows the storage of only active actors.
	
	static std::unordered_map< IDType, Identification * > ActorsByID;
	
	// Since these three elements are shared among all actors, and actors can 
	// be created by other actors in the message handlers or otherwise, the
	// elements can be operated upon from different threads. It is therefore 
	// necessary to ensure sequential access by locking a mutex.
	
	static std::mutex InformationAccess;
		
public:	

	// There are static functions to obtain an address by name or by numerical 
	// ID. They are not defined in-line since they return an address class which 
	// is only forward declared..
	
	static Address Lookup( const std::string ActorName );
	static Address Lookup( const IDType TheID );
	
	// Other actors may need to obtain a pointer to this actor, and this can 
	// only be done through a legal address object. It is not declared in-line 
	// since it uses the internals of the actor definition. This function will 
	// throw invalid_argument if the address is not for a valid and running actor.
	
	static Actor * GetActor( Address & ActorAddress );
		
	// When an address is created, it needs to register with the identification 
	// object representing the actor. A local actor on this endpoint will 
	// remember registrations and make sure that they are invalidated when the 
	// actor is destroyed. A remote address will simply record the number of 
	// references and since it is dynamically allocated, remove the ID when 
	// the last address is removed.
	
	virtual void Register(   Address * NewAddress ) = 0;
	virtual void DeRegister( Address * OldAddress ) = 0;
	
	// The constructor is protected since it can only be used by derived classes,
	// and it only assigns the ID and the name. If the name string is not given
  // a default name "ActorNN" will be assigned where the NN is the number of
	// the actor. It is the responsibility of the derived class to ensure that 
	// the provided name is unique, and to update the maps to return the actor 
	// pointer by name or ID.
	
	Identification( Actor * TheActor, 
								  const std::string & ActorName = std::string() )
	: NumericalID( GetNewID() ),  Name( ActorName.empty() ? 
		  "Actor" + std::to_string( NumericalID ) : ActorName ),
    ActorPointer( TheActor )
	{ }

	virtual ~Identification( void )
	{ }
	
};

// Each actor has an identification derived from the basic identification. It
// will ensure that the name given to the actor is unique, and if it is not 
// unique, it will throw an invalid argument exception from the constructor. 

class EndpointIdentity : public Identification
{
private:
	
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
	
	// Theoretically addresses can be copied or deleted in different threads so
	// access to the address set must be protected by a mutex. It must be 
	// recursive because when the Endpoint Identity closes it will call the 
	// invalidate function on each of its addresses, and hence it needs to lock 
	// the access to the address set. However as the addresses are invalidated, 
	// they will de-register which implies that the lock will be acquired also 
	// in the de-registration function. It will  be from the same thread, but it 
	// will block unless the mutex accepts multiple (recursive) locks.
	
	std::recursive_mutex AddressAccess;
	
	// And there are two functions to register and de-register an address object
	
public:
	
	virtual void Register(   Address * NewAddress );
	virtual void DeRegister( Address * OldAddress );
	
	// Constructor and destructor
	//	
	// In order to fully register the actor, a pointer to the actor is needed.
	// However, the actor identification object is supposed to be an element of 
	// the actor and hence it should be constructed prior to running the actor's
	// constructor. The "this" pointer will exist for the actor's memory area 
	// and it should be possible to store it, however some compilers may rightly 
	// give a warning. If no name is given to the actor, it will be given the 
	// name "ActorNN" where NN is the numerical ID of the actor.

	EndpointIdentity( Actor * TheActor, 
											 const std::string & ActorName = std::string() );
	
	// The destructor method cannot be defined in-line since it will call 
	// methods on the addresses to invalidate them and the address class is 
	// not yet fully specified.
	
	virtual ~EndpointIdentity( void );
	
} ActorID;

// The remote identity of an actor is basically a string, and the actor pointer
// is set to the local actor that has registered as the Session Layer. The 
// session layer is responsible for serialising and de-serialising message that
// can then be transmitted as text strings to remote network endpoints. 
// 
// This implies that remote identities are created from a string setting the 
// name of the remote actor. This name is registered in the name database 
// because no local actor with the same name should be created. The Remote 
// identity class is responsible for forwarding the messages to the session 
// layer, and trying to create a remote identity before the session layer 
// server has been set will result in a standard logic error exception.

class RemoteIdentity : public Identification
{
private:
	
	static Actor * TheSessionLayerServer;
	
public:
	
	static void  SetSessionLayerServer( Actor * TheSever );
	
	// The identity will always be dynamically allocated. However, since addresses
	// can be copied, new copies will just inherit the pointer to the 
	// identification, and this identification should not be deallocated before 
	// the last address using it de-registers. it is therefore a private counter
	// that counts the number of addresses referencing this identity.
	
private:
	
	unsigned int NumberOfAddresses;
	
	// This counter is increased when the address register, and decreased when 
	// the actor de-register. If the de-registration leads to the counter 
	// reaching zero, this ID will be destructed.
	
public:
	
	virtual void Register(   Address * NewAddress );
	virtual void DeRegister( Address * OldAddress );
	
	// If the session layer exist, then the the remote actor is registered in the 
	// map of known addresses if it does not already exist. If there are no 
	// session layer, external communication is not possible and a standard 
	// logic error exception is thrown.
		
	RemoteIdentity( const std::string & ActorName );
	
	// The destructor does nothing but is needed for completeness
	
	virtual ~RemoteIdentity( void )
	{ }
};
		
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
	
	Identification * TheActor;
	
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
	
	// The identity class is allowed to read the actor pointer, and the endpoint
	// identity is allowed to invalidate a reference if the endpoint actor is 
	// closing.

	friend class Identification;	
	friend class EndpointIdentity;
	
public:
	
	// The constructor takes the pointer to the actor identification class of 
	// the actor this address refers to, and then register with this actor 
	// if the pointer given is not Null.
	
	inline Address( Identification * ReferencedActor = nullptr )
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
	: Address( Identification::Lookup( Name ) )
	{
		// The actor name is unknown, an empty address will be returned and a new
		// remote identity must be created for this name.
		
		if ( TheActor == nullptr )
			TheActor = new RemoteIdentity( Name );
	}
	
	// Oddly enough, but Theron has no constructor to get an address by the 
	// numerical ID of the actor, so a similar one is provided now.
	
	inline Address( const Identification::IDType & ID )
	: Address( Identification::Lookup( ID ) )
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
	
	inline Identification::IDType AsInteger( void ) const
	{
		return TheActor->NumericalID;
	}
	
	inline Identification::IDType AsUInt64( void ) const
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
// registered message handlers. The To address is not needed for messages sent
// to local actors, although it can be useful for debugging purposes. The To
// address is sent with the message for remote communication so that the 
// remote endpoint can deliver the message to the right actor.
	
class GenericMessage
{
public:
	
	const Address To, From;
	
	inline GenericMessage( const Address & Receiver, const Address & Sender )
	: To( Receiver ), From( Sender )
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

	Message( const std::shared_ptr< MessageType > & MessageCopy, 
					 const Address & From, const Address & To )
	: GenericMessage( To, From ), TheMessage( MessageCopy )
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
