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
#include <utility>						// Pairs
#include <unordered_map>			// To map actor names to actors
#include <set>							  // To keep track of actor addresses
#include <functional>					// For defining handler functions
#include <list>								// The list of message handlers
#include <algorithm>					// Various container related utilities
#include <thread>						  // To execute actors
#include <condition_variable> // To synchronise threads
#include <atomic>							// Thread protected variables
#include <stdexcept>				  // To throw standard exceptions
#include <sstream>						// To provide nice exception messages


namespace Theron {
	
// For compatibility reasons there must be a class called Framework and an actor
// has a method that returns a pointer to it. Basically the actor is fully 
// replacing the framework. All compatibility classes are defined at the end.

class Framework;

// There are enumerations for a yield strategy, and since the scheduling is 
// left for the operating system, these strategies will have no effect.

enum YieldStrategy 
{
  YIELD_STRATEGY_CONDITION = 0,
  YIELD_STRATEGY_HYBRID,
  YIELD_STRATEGY_SPIN,
  YIELD_STRATEGY_BLOCKING = 0,
  YIELD_STRATEGY_POLITE = 0,
  YIELD_STRATEGY_STRONG = 1,
  YIELD_STRATEGY_AGGRESSIVE = 2
};

// The EndPoint is forward declared because it is used as an argument to one of
// the Framework constructors.

class EndPoint;

// In this implementation everything is an actor as there is no compelling 
// reason for the other Theron classes. 

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

// -----------------------------------------------------------------------------
// Generic identification
// -----------------------------------------------------------------------------
//
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
	
	static Actor * GetActor( const Address & ActorAddress );
	
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

	// The destructor removes the named actor and the ID from the registries, 
	// and since it implies a structural change of the maps, the lock must be 
	// acquired. It is automatically released when the destructor terminates.
	
	virtual ~Identification( void )
	{ }
};

// -----------------------------------------------------------------------------
// Endpoint identity
// -----------------------------------------------------------------------------
//
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

// -----------------------------------------------------------------------------
// Remote identity
// -----------------------------------------------------------------------------
//
// The remote identity of an actor is basically a string, and the actor pointer
// is set to the local actor that has registered as the Presentation Layer. The 
// presentation layer is responsible for serialising and de-serialising message 
// that can then be transmitted as text strings to remote network endpoints. 
// 
// This implies that remote identities are created from a string setting the 
// name of the remote actor. This name is registered in the name database 
// because no local actor with the same name should be created. The Remote 
// identity class is responsible for forwarding the messages to the presentation 
// layer, and trying to create a remote identity before the presentation layer 
// server has been set will result in a standard logic error exception.

class RemoteIdentity : public Identification
{
private:
	
	static Actor * ThePresentationLayerServer;
	
public:
	
	static void  SetPresentationLayerServer( Actor * TheSever );
	
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
	// closing. The Actor class is also allowed to access this pointer
	
  friend class Actor;
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

	// There is an assignment operator that basically makes a copy of the 
	// other Address. However, since it is called on an already existing 
	// address it must de-register the address if it is a proper address.
	
	inline Address & operator= ( const Address & OtherAddress )
	{
		if ( TheActor != nullptr ) TheActor->DeRegister( this );
		
		TheActor = OtherAddress.TheActor;
		
		if ( TheActor != nullptr ) TheActor->Register( this );
		
		return *this;
	}
	
	// It should have a static function Null to allow test addresses
	
	static const Address Null( void )
	{	return Address();	}
	
	// There is an implicit conversion to a boolean to check if the address is 
	// valid or not.
	
	operator bool (void ) const
	{	return TheActor != nullptr;	}
	
	// There are comparison operators to allow the address to be used in standard
	// containers.
	
	bool operator == ( const Address & OtherAddress ) const
	{ return TheActor == OtherAddress.TheActor; }
	
	bool operator != ( const Address & OtherAddress ) const
	{ return TheActor != OtherAddress.TheActor; } 

	// The less-than operator is more interesting since the address could be 
	// Null and how does that compare with other addresses? It is here defined 
	// that a Null address will be larger than any other address since it is 
	// assumed that this operator defines sorting order in containers and then 
	// it makes sense if the Null operator comes last.
	
	bool operator < ( const Address & OtherAddress ) const
	{
		if ( TheActor == nullptr )
			return true;
		else 
			if ( OtherAddress.TheActor == nullptr ) return true;
		  else 
				return TheActor->NumericalID < OtherAddress.TheActor->NumericalID;
	}
		
	// Then it must provide access to the actor's name and ID that can be 
	// taken from the actor's stored information.
	
	inline std::string AsString( void ) const
	{	return TheActor->Name;	}
	
	inline Identification::IDType AsInteger( void ) const
	{	return TheActor->NumericalID;	}
	
	inline Identification::IDType AsUInt64( void ) const
	{	return AsInteger();	}
	
	// There is a legacy function to obtain the numerical ID of the framework, 
	// which is here taken identical to the actor pointed to by this address. 
	// It will throw a logic error if it is a null address.
	
	inline Identification::IDType GetFramework( void ) const
	{
		if ( TheActor == nullptr )
			throw std::logic_error( "Null address has no framework!" );
		
	  return TheActor->NumericalID;
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

inline Address GetAddress( void ) const
{	
	return Identification::Lookup( ActorID.NumericalID ); 
}

// There is another function to check if a given address is a local actor. 
// Again RTTI is used when trying to cast the identification pointer to a local
// address and if this is successful the actor is taken to be local.

inline bool IsLocalActor( const Address & RequestedActorID ) const
{
	Identification   * TheActorID      = RequestedActorID.TheActor;
	EndpointIdentity * VerifiedPointer = 
															  dynamic_cast< EndpointIdentity *>( TheActorID );
  
  if ( VerifiedPointer == nullptr )
		return false;
	else
		return true;
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
//	
// The message stores the address of the sending actor, and has a method to 
// be used by the queue handler when checking if it can be forwarded to the 
// registered message handlers. The To address is not needed for messages sent
// to local actors, although it can be useful for debugging purposes. The To
// address is sent with the message for remote communication so that the 
// remote endpoint can deliver the message to the right actor.

protected:
	
class GenericMessage
{
public:
	
	const Address From, To;
	
	inline GenericMessage( const Address & Sender, const Address & Receiver )
	: From( Sender ), To( Receiver )
	{ }
	
	// It is important to make this class polymorphic by having at least one 
	// virtual method, and it must in order to ensure proper destruction of the 
	// derived messages.
	
	virtual ~GenericMessage( void )
	{ }
};

// Each actor has a queue of messages and add new messages to the end and 
// consume from the front of the queue. It can therefore be implemented as a 
// standard queue.

using MessageQueue = std::queue< std::shared_ptr< GenericMessage > >;

// The size type is defined as it is convenient to use for anything that has 
// to do with the message queue

using MessageCount = MessageQueue::size_type;

// The type specific message will define the function to process the message 
// by first trying to cast the handler to the handler templated for the 
// actual message type, and if successful, it will invoke the handler function.

private:
	
template< class MessageType >
class Message : public GenericMessage
{
public:
	
	const std::shared_ptr< MessageType > TheMessage;

	Message( const std::shared_ptr< MessageType > & MessageCopy, 
					 const Address & From, const Address & To )
	: GenericMessage( From, To ), TheMessage( MessageCopy )
	{ }
	
	virtual ~Message( void )
	{ }
};

// Each actor has a message queue 

MessageQueue Mailbox;

// Since this queue will be written to by the sending actors and processed 
// by this actor, there may be several threads trying to operate on the queue 
// at the same time, so sequential access must be ensured by a mutex.

std::mutex QueueGuard;

// Messages are queued a dedicated function that obtains a unique lock on the 
// queue guard before adding the message. The return type could be used to 
// indicate issues with the queuing, but the function should really throw
// an exception in case of serious errors.

protected: 
	
virtual 
bool EnqueueMessage( const std::shared_ptr< GenericMessage > & TheMessage );

// Theron provides a function to check the number of queued messages, which 
// essentially only returns the current queue size, including the message 
// being handled. There is no need to acquire a lock because the function only
// reads the value and makes no structural changes to the queue. 

public:

inline MessageCount GetNumQueuedMessages( void ) const
{ return Mailbox.size(); }

// -----------------------------------------------------------------------------
// Sending messages
// -----------------------------------------------------------------------------
//
// The main send function allocates a new message of the provided type and 
// enqueues this with the receiving actor. This is the version of the send 
// function found in the Framework in Theron.

public:

template< class MessageType >
inline bool Send( const MessageType & TheMessage, 
								  const Address & Sender, const Address & Receiver )
{
	Actor * ReceivingActor = Identification::GetActor( Receiver );
	auto    MessageCopy    = std::make_shared< MessageType >( TheMessage );
	
	return	
	ReceivingActor->EnqueueMessage( std::make_shared< Message< MessageType> >(	
																	MessageCopy, Sender, Receiver	));
}

// Theron's actor has a simplified version of the send function basically just 
// inserting its own address for the sender's address.

protected:
	
template< class MessageType >
inline bool Send( const MessageType & TheMessage, const Address & Receiver )
{
	return Send( TheMessage, GetAddress(), Receiver );
}

/*=============================================================================

 Handlers

=============================================================================*/

// The Generic Message class is only supposed to be a polymorphic place holder
// for the type specific messages constructed by the send function. It provides
// an interface for a generic message handler, and will call this if its pointer
// can be cast into the type specific handler class (see below). 

private:

class GenericHandler
{
public:
	
	enum class State
	{
		Normal,
		Executing,
		Deleted
	};
	
protected:
	
	State CurrentStatus;
	
public:
	
	// A function to get the handler state.
	
	inline State GetStatus( void )
	{	return CurrentStatus;	}
	
	// And another one to set the status that is only used from the de-registration
	// function if this this handler should be deleted.
	
	inline void SetStatus( const State NewState )
	{ CurrentStatus = NewState; }
	
	// There is a function to execute the handler on a given message
	
	virtual bool ProcessMessage( 
													std::shared_ptr< GenericMessage > & TheMessage ) = 0;
	
	// The constructor simply sets the status to normal
	
	GenericHandler( void )
	{ CurrentStatus = State::Normal; }
	
	virtual ~GenericHandler( void )
	{ }
};

// The actual type specific handler is a template on the message type handled  
// by the function. It remembers the handler function and tries to convert the 
// message to the right type. If this is possible, it will invoke the handler
// function.

template< class ActorType, class MessageType >
class Handler : public GenericHandler
{
public:
	
	// The handler must store the function to process the message. This has the 
	// same signature as the actual handler, and it is specified to call the 
	// function on the actor having this handler.
	
	void (ActorType::*HandlerFunction)( const MessageType &, const Address );
	
	// The pointer to the actor having this handler function is also stored 
	// since it is the easiest way to make sure it is the right actor type.
	
	ActorType * const TheActor;
		
	virtual bool ProcessMessage( std::shared_ptr< GenericMessage > & TheMessage )
	{
		std::shared_ptr< Message< MessageType > > TypedMessage = 
							std::dynamic_pointer_cast< Message< MessageType > >( TheMessage );
		
		if ( TypedMessage )
		{
			// The message could be converted to this message type, and the handler 
			// can be invoked through the handler pointer on the stored actor
			
			(TheActor->*HandlerFunction)( *(TypedMessage->TheMessage), 
																	    TypedMessage->From );
			return true;
		}
		else 
			return false;
	}
	
	// The constructor stores the handler function.
	
	Handler( ActorType * HandlingActor,
     void (ActorType::*GivenHandler)( const MessageType &, const Address ))
	: GenericHandler(), HandlerFunction( GivenHandler ), TheActor( HandlingActor )
	{ }
	
	// There is a copy constructor to allow this to be used with standard 
	// containers
	
	Handler( const Handler<ActorType, MessageType> & OtherHandler )
	: GenericHandler(), HandlerFunction( OtherHandler.HandlerFunction ),
	  TheActor( OtherHandler.TheActor )
	{ }
	
	virtual ~Handler( void )
	{ }
};

// The list of handlers registered for this actor is kept in a simple list, and
// The transposition rule suggested by Ronald Rivest (1976): "On self-organizing 
// sequential search heuristics", Communications of the ACM, Vol. 19, No. 2, 
// pp. 63-67 is used to promote message handlers that receive the more messages. 
// Here a successful message handler will be swapped with the element 
// immediately in front unless the element is the handler at the start of the 
// list, and new handlers are added to the end of this list.
// The default handler treats the message as a generic handler.
//
// It should be remarked that there is no mutex to protect this list since 
// it is use to execute the message handlers, and typically additional handlers
// or deletions of handlers will take place from within the one executing 
// handler, which is running in the same thread as the Dispatch Messages 
// function

std::list< std::shared_ptr< GenericHandler > > MessageHandlers;

// -----------------------------------------------------------------------------
// Normal handler registration
// -----------------------------------------------------------------------------
//
// Registration of handlers is a matter of adding the function call to the 
// list of handlers for the given actor. Essentially the actor pointer is 
// not necessary because the handler should be registered for this actor. 
// However, it seems necessary in order to ensure that the pointer to the 
// handler function is a pointer to a function on the actor it will actually
// be called for. Note also that the same handler can be registered multiple 
// times, and many handlers can be registered for the same message.

protected:

template< class ActorType, class MessageType >
inline bool RegisterHandler( ActorType  * const TheActor, 
							 void ( ActorType::* TheHandler)(	const MessageType & TheMessage, 
																								const Address From ) )
{
	TheActor->MessageHandlers.push_back( 
	std::make_shared< Handler< ActorType, MessageType > >(TheActor, TheHandler) );
	
	return true;
}

// -----------------------------------------------------------------------------
// Normal handler de-registration
// -----------------------------------------------------------------------------
//
// Since there are no checks that a function is only registered as a hander 
// only once, the full list of handlers must be processed and all handlers 
// whose pair of handling actor and handler function match the registered 
// handler.

template< class ActorType, class MessageType >
inline bool DeregisterHandler( ActorType  * const HandlingActor, 
							    void (ActorType::* TheHandler)(const MessageType & TheMessage, 
																								 const Address From ) )
{
	// To compare the actor and the handler function pointer, it is necessary to
	// know that that the handler is of the right type, and RTTI is used for 
	// this by trying to cast dynamically the handler to a pointer to the 
	// a message handler for the actor and message type. This is done by a 
	// comparator function.
	//
	// A subtle point is that even if the handler should be removed, it cannot 
	// be removed if it is the currently running handler. In this case, its state
	// will be set to deleted, but the comparator will (wrongly!) return false 
	// to prevent the removal of that particular handler before it has terminated.
	
	auto ToBeRemoved = [=]( const std::shared_ptr< GenericHandler > & AnyHandler )
	->bool{
		std::shared_ptr< Handler< ActorType, MessageType > > TypedHandler =
		 std::dynamic_pointer_cast< Handler< ActorType, MessageType > >(AnyHandler);
			
		if (  TypedHandler && 
			  ( TypedHandler->TheActor == HandlingActor ) &&
				( TypedHandler->HandlerFunction == TheHandler ) )
		{
			// The handler is identical and can be removed unless it is executing
			if ( AnyHandler->GetStatus() == GenericHandler::State::Executing )
		  {
				AnyHandler->SetStatus( GenericHandler::State::Deleted );
				return false;
			}
			else 
				return true;
		}
		else
			return false;
	};
	
	// In order to return an indication whether something was removed or not,
	// the size of the handler list must be checked before and after the 
	// removal.
	
	auto InitialHandlerCount = HandlingActor->MessageHandlers.size();
	
	// With the comparator it is easy to remove the handlers of this type
	
	HandlingActor->MessageHandlers.remove_if( ToBeRemoved );
	
	// Then a meaningful feedback can be given
	
	if ( InitialHandlerCount == HandlingActor->MessageHandlers.size() )
		return false;
	else
		return true;
}

// -----------------------------------------------------------------------------
// Checking registration
// -----------------------------------------------------------------------------
//
// The very same mechanism of the method to de-register a message handler can 
// be used to test if a particular handler is already registered, and it is 
// almost an exact copy of the previous method.

template< class ActorType, class MessageType >
inline bool IsHandlerRegistered ( ActorType  * const HandlingActor, 
							    void (ActorType::* TheHandler)(const MessageType & TheMessage, 
																								 const Address From ) )
{
	// To compare the actor and the handler function pointer, it is necessary to
	// know that that the handler is of the right type, and RTTI is used for 
	// this by trying to cast dynamically the handler to a pointer to the 
	// a message handler for the actor and message type. This is done by a 
	// comparator function.
	
	auto IsHandler = [=]( const std::shared_ptr< GenericHandler > & AnyHandler )
	->bool{
		std::shared_ptr< Handler< ActorType, MessageType > > TypedHandler =
		 std::dynamic_pointer_cast< Handler< ActorType, MessageType > >(AnyHandler);
			
		if (  TypedHandler && 
			  ( TypedHandler->TheActor == HandlingActor ) &&
				( TypedHandler->HandlerFunction == TheHandler ) )
			return true;
		else
			return false;
	};

	// Then the standard search algorithm can be used to find an occurrence of 
	// the given actor and handler function.
	
	return 
	std::any_of( HandlingActor->MessageHandlers.begin(), 
							 HandlingActor->MessageHandlers.end(), IsHandler );
}

// -----------------------------------------------------------------------------
// Default handler registration
// -----------------------------------------------------------------------------
//
// The default handler treats the message as a generic object, and it supports
// both variants of the default handler type.

private:

std::shared_ptr< GenericHandler > DefaultHandler;

// There are two versions of the default handler, one that only takes the 
// address of the actor sending the message.

template< class ActorType >
class DefaultHandlerFrom : public GenericHandler
{
private:
	
	void (ActorType::*HandlerFunction)( const Address );
	ActorType * TheActor;
	
public:
	
	virtual bool ProcessMessage( std::shared_ptr< GenericMessage > & TheMessage )
	{
		(TheActor->* HandlerFunction)( TheMessage->From );
		return true;
	}
	
	inline DefaultHandlerFrom( ActorType * HandlingActor, 
											const void (ActorType::*GivenHandler)( const Address ) )
	: GenericHandler(), HandlerFunction( GivenHandler ), TheActor( HandlingActor )
	{ }
	
	virtual ~DefaultHandlerFrom( void )
	{ }
};

// And the function to register the handler

protected:

template< class ActorType >
inline bool SetDefaultHandler( ActorType  *const TheActor, 
											  void ( ActorType::*TheHandler )( const Address From ))
{
	TheActor->DefaultHandler = std::make_shared< DefaultHandlerFrom< ActorType> >( 
														 TheActor, TheHandler );
	
	return true;
}

// The alternative callback function takes a void data pointer, its size and 
// the from actor. This allows us to try to identify the type of the message 
// by sending the type name of the message to the handler as a string. Hence,
// the void pointer can safely be recast into a standard string pointer. 

private: 
	
template< class ActorType >
class DefaultHandlerData : public GenericHandler
{
private:
	
	void (ActorType::*HandlerFunction) ( const void *const, const uint32_t, 
																			 const Address );
	ActorType * TheActor;
											 
public:
	
	virtual bool ProcessMessage( std::shared_ptr< GenericMessage > & TheMessage )
	{
		std::ostringstream ErrorMessage;
		
		ErrorMessage << "Message type is " << typeid( TheMessage ).name();
		
		std::string Description( ErrorMessage.str() );
		
		(TheActor->*HandlerFunction)( &Description, sizeof( Description ), 
																  TheMessage->From );
		
		return true;
	}
	
	inline DefaultHandlerData( ActorType * HandlingActor, 
				 void (ActorType::*GivenHandler) ( 
				 const void *const, const uint32_t, const Address )  )
	: GenericHandler(), HandlerFunction( GivenHandler ), TheActor( HandlingActor )
	{	}
	
	virtual ~DefaultHandlerData( void )
	{ }
};

// And the function to set this kind of handler.

protected:

template< class ActorType >
inline bool SetDefaultHandler( ActorType *const TheActor,
	     void (ActorType::*TheHandler)( const void *const Data, 
																			const uint32_t Size, const Address From ))
{
 	TheActor->DefaultHandler = std::make_shared< DefaultHandlerData< ActorType > >(
													   TheActor, TheHandler );
	return true;
}

/*=============================================================================

 Execution control

=============================================================================*/

// Execution of the handlers for queued messages will take place in a dedicated
// thread that will be started from the method to enqueue messages if the 
// arriving message is the first message in the queue.

private: 
	
std::thread Postman;

// This thread will execute the following function that will take out the 
// first message from the queue and call the handler for this message. If 
// none of the registered handlers are able to process the message, then the 
// message will be delivered to the default handler. If no default handler 
// exists, then a logical error is thrown.

void DispatchMessages( void );

// There is a potential issue if a message given to an actor that has no 
// registered handler for the type of message. There are then two options: 
// the dispatch function can throw an exception indicating the type of the 
// message, or it may simply tacitly ignore the message entirely if the 
// application is able to manage this. Different actors are allowed to 
// apply different policies.

protected:
	
enum class MessageError
{
	Throw,
	Ignore
} MessageErrorPolicy;

// There is a small helper function that waits for the Postman to complete the 
// delivery of messages, i.e. wait for the mailbox to drain. Essentially, it 
// just joins the work of the Postman.

inline void DrainMailbox( void )
{
	if ( Postman.joinable() )
		Postman.join();
}

// -----------------------------------------------------------------------------
// Synchronisation
// -----------------------------------------------------------------------------
//
// In Theron there is a dedicated Receiver object which is here merged with 
// the actor since it is useful that also actors are able to wait for the 
// condition that one or more messages arrives. 
// 
// One should observe that even though the Wait function will be defined on 
// one actor, it can be called from many different actors running in multiple 
// threads. The Wait function should block until the given number of messages 
// has been received by the actor, and different invocations of the Wait 
// function may wait for different number of messages.
// 
// Thus the only central coordination point is the Postman, which may not even 
// be started when a Wait function is called, and the Postman may stop if there
// are no more messages to process. 
//
// It is a matter of definition whether the Receiver behaviour should be 
// replicated or not. A Receiver will not process any messages before the Wait
// function is called, while the natural behaviour is that message processing 
// would run continuously until the first Wait function is called, and then 
// message by message until the Wait has received the desired count of messages
// before the remaining messages are continuously processed again. The latter 
// functionality is implemented here: Only messages to be processed after the 
// wait function is triggered will be blocked. 
//
// The basic principle for the synchronisation is a variable to count the 
// number of Wait functions. If this counter is zero, the Postman will just 
// process messages. If this counter is larger than zero, the postman will 
// switch into message-by-message processing mode until this counter reaches 
// zero again.

private:
	
unsigned int WaiterCount;

// The question is whether the Postman should halt processing before the new 
// message is processed or after the message has been processed, or both. 
// Theron's documentation say: "a call to Wait will return immediately if the 
// call can be satisfied by an unconsumed message that has already arrived. 
// This means that a caller wishing to synchronize with the arrival of an 
// expected message can safely call Wait, irrespective of whether some or all 
// of a number of expected messages may have already arrived. If one or more 
// messages have arrived the call simply returns immediately without blocking, 
// consuming all arrived messages up to the specified maximum. Otherwise it 
// blocks until a message arrives, whereupon the thread is woken and returns."
// In other words, the postman should halt when the message arrives, notify 
// all Wait threads, and then continue with the processing of the message. 
// However, this does not correspond to the Consume function, and it is 
// illogical since the waiting thread could start processing before the 
// receiving agent has processed the incoming message and thereby the agent 
// waited for may have the same state as before the wait started. A further 
// complication is if the agent is serving multiple messages - in this case 
// it is necessary to check that the right message has arrived, and then the 
// message must be processed when wait returns. In conclusion: The notification
// to the threads waiting for a message will happen AFTER the message has been 
// consumed by the actor.
// 
// This requires one condition variable for the Wait threads to use when waiting
// for the next message, and one mutex that can be used to set up the wait or
// notify the waiting Wait functions. Note that the mutex must also be used to
// protect updates to the count of the Wait functions above.

std::condition_variable OneMessageArrived;
std::mutex 							WaitGuard;

// It is best practice to ensure that the condition variable is not triggered 
// by some other reason that the arrival of a new message, and therefore 
// there is a flag indicating that a message really has arrived. It should be 
// changed only by the Postman when holding the guard locked.

bool NewMessage;

// Since each Wait function runs in a different thread, which is again different
// from the Postman's thread, it could be that the postman would have handled
// the message and move on to the next message before all the Waiting threads 
// have had the opportunity to process the notification of the previous message.
// An acknowledgement protocol is therefore implemented, where each Wait will 
// increase the acknowledgement count, and notify the Postman. When the 
// acknowledgement count equals the number of wait functions, the Postman will 
// proceed to dispatch the message to the right handler(s).

unsigned int 						AcknowledgementCount;
std::condition_variable ContinueMessageProcessing;

// It could be that a wait is still active when the actor terminates. The wait 
// must then be aborted, and there is specific flag for this that is checked 
// by the Wait method when it regains control. 

bool AbortWait;

// The wait function is defined as it is for Theron, with an optional number 
// of messages to wait for. Note that in contrast with the receiver, an actor 
// will quietly process messages until this function is called, and it will 
// continue to process messages after the Wait terminates. 

public:
	
MessageCount Wait( const MessageCount MessageLimit = 1 );

/*=============================================================================

 Constructors and destructor

=============================================================================*/

// The actor allows the a user defined name to be given, and if it is omitted 
// it will be assigned by default as "ActorNN" where NN is the numerical ID of 
// the actor.

inline Actor( const std::string & ActorName = std::string() )
: ActorID( this, ActorName ), 
  Mailbox(), QueueGuard(), 
  MessageHandlers(), DefaultHandler(), Postman(), 
  WaiterCount(0), OneMessageArrived(), WaitGuard(), 
  AcknowledgementCount(0), ContinueMessageProcessing()
{
	// The flags for message processing is set by default to false as there is no
	// message that has arrived yet
	
	NewMessage = false;
	
	// The default error handling policy is to throw on unhanded messages
	
	MessageErrorPolicy = MessageError::Throw;
}

// The destructor needs to consider the situation where the actor is destroyed 
// while there is an active wait for more messages. It must also wait until 
// the processing of all messages has taken place by the Postman.

virtual ~Actor( void );

/*=============================================================================

 Compatibility Framework

=============================================================================*/

// One constructor to use with the framework, which has no effect here

Actor( Framework & TheFramework, std::string TheName = std::string() )
: Actor( TheName )
{ }

// It is currently impossible to exclude the framework completely and so there 
// most be a framework somewhere, and in order to know where, the address of 
// this specific actor is stored with all actors as a static variable.

private:

static Framework * GlobalFramework;

// The Framework must also be a friend to be allowed to set this pointer when 
// it is created.

friend class Framework;

// The function returning the framework will therefore really only return this 
// pointer de-referenced.

public: 
	
inline Framework & GetFramework( void ) const
{ return *GlobalFramework; }


};			// Class Actor

/*=============================================================================

 Compatibility definitions

=============================================================================*/

// The addresses in Theron are top level objects, although this is conceptually
// wrong as an address cannot exist without the actors. However, in order to 
// satisfy legacy references to Theron address they are re-defined as own 
// objects.

using Address = Actor::Address;

// The Endpoint class really only stores the name of this endpoint

class EndPoint
{
public:
	
	// Empty parameters as they are in Theron
	
  class Parameters
	{ 
	public:
		
		Parameters( void )
		{ }
	};
	
private:
	
	std::string EndPointName, EndPointLocation;
	
public:
	
	// The only public method is the one to obtain the name of the endpoint
	
	inline std::string GetName( void )
	{ return EndPointName; }

	// There is also a connect function, but this is anyway deferred to the 
	// Network endpoint (see that class file)
		
	EndPoint( const char * const Name, const char * const Location, 
						const Parameters params = Parameters() )
	: EndPointName( Name ), EndPointLocation( Location )
	{ }
	
	~EndPoint( void )
	{ }
};

// The Framework has a set of parameters mainly concerned with the the 
// scheduling of actors, which is here left for the operating system entirely

class Framework : public Actor
{
public: 
	
	class Parameters
	{
	public:
		
		unsigned int  mThreadCount, 
								  mNodeMask,
								  mProcessorMask;
	  YieldStrategy mYieldStrategy;
		float         mThreadPriority;

		Parameters( const uint32_t threadCount = 16, const uint32_t nodeMask = 0x1, 
								const uint32_t processorMask = 0xFFFFFFFF, 
							  const YieldStrategy yieldStrategy 
																		  = YieldStrategy::YIELD_STRATEGY_CONDITION, 
							 const float priority=0.0f )
		: mThreadCount( threadCount ), mNodeMask( nodeMask ), 
		  mProcessorMask( processorMask ), mYieldStrategy( yieldStrategy ),
		  mThreadPriority( priority )
		{ }
	};

	// The following set of parameter related functions have absolutely no effect
	// since the framework does nothing

	inline void SetMaxThreads(const unsigned int count)
	{ }

	inline void SetMinThreads(const unsigned int count)
	{ }

	inline unsigned int GetMaxThreads( void ) const
	{ return 1; }

	inline unsigned int GetMinThreads( void ) const 
	{ return 1; }

	inline unsigned int GetNumThreads( void ) const 
	{ return 1; }

	inline unsigned int GetPeakThreads( void ) const 
	{ return 1; }

	inline unsigned int GetNumCounters( void ) const
	{ return 0; }

	inline void ResetCounters( void )
	{ }

	// The Theron Framework has a method to set the fall back handler with respect 
	// to an actor. However, this is no different from the actors default hander, 
	// and it is therefore defined as a implicit call on those functions. It must 
	// be defined for both version of the fall back handler arguments. 

	template< class ActorType >
	inline bool SetFallbackHandler( ActorType  *const TheActor, 
												  void ( ActorType::*TheHandler )( const Address From ))
	{
		return Actor::SetDefaultHandler( TheActor, TheHandler );
	}

	template <class ActorType>
	inline bool SetFallbackHandler( ActorType *const TheActor,
   void (ActorType::*TheHandler)( const void *const Data, 
																	const uint32_t Size, const Address From ))
	{
		return Actor::SetDefaultHandler( TheActor, TheHandler );
	}

	// The framework constructors mainly delegates to the Actor constructor to 
	// do the work. When no name is passed, the actor is called "Framework", which 
	// should prevent the construction of two actors with the same name.

	Framework( const unsigned int ThreadCount )
	: Actor( "Framework" )
	{ 
		Actor::GlobalFramework = this;
	}

	Framework( const Parameters & params )
	: Actor( "Framework" )
	{
		Actor::GlobalFramework = this;
	}

	Framework( EndPoint & endPoint, const char *const name = 0, 
						 const Parameters & params = Parameters() )
	: Actor( std::string( name ).empty() ? "Framework" : name )
	{ 
		Actor::GlobalFramework = this;
	}
};
// -----------------------------------------------------------------------------
// Receiver
// -----------------------------------------------------------------------------
//
// The receiver should essentially be unnecessary, however the functionality is 
// slightly different from an actor the way it is described in Theron where it 
// seems to put all received messages on hold and consume them with either the 
// consume message or the wait message. It is therefore implemented similarly 
// here with a separate message queue.

class Receiver : public Actor
{
private:
	
	// Buffered messages are kept in a message queue, and there is a mutex to 
	// protect access to this queue.
	
	MessageQueue MessageBuffer;
	std::mutex   BufferGuard;
	
	// There is a waiting flag which is true if the wait function is invoked and 
	// there are not sufficient messages buffered. In this case, the enqueue 
	// method should not buffer incoming messages but let them through to the 
	// actor for immediate processing.
	
	bool Waiting;
	
protected:
	
	// The Actor's enqueue is captured and the delivered message is just put in 
	// the message buffer to be processed by the Wait or the Consume methods.
	
	virtual 
	bool EnqueueMessage( const std::shared_ptr< GenericMessage > & TheMessage )
	{
		std::lock_guard< std::mutex > Lock( BufferGuard );
		
		if ( Waiting )
			Actor::EnqueueMessage( TheMessage );
		else
			MessageBuffer.push( TheMessage );
		
		return true;
	}

public:
	
	// The Consume function takes a number of messages up to the message limit 
	// or to the end of the buffer and hands these over to the Actor's enqueue 
	// function for processing. After passing on the messages it will wait for 
	// the Postman to complete the processing of these messages before terminating
	// with the number of messages that could be processed.
	
	MessageCount Consume( MessageCount MessageLimit )
	{
		std::lock_guard< std::mutex > Lock( BufferGuard );
		
		MessageCount TheCount  = std::min( MessageBuffer.size(), MessageLimit ),
								 Served = 0;
		
		while ( TheCount > 0 )
		{
			Actor::EnqueueMessage( MessageBuffer.front() );
			MessageBuffer.pop();
			TheCount--;
			Served++;
		}

		// In order to ensure ordered delivery of messages, the processing of the 
		// buffered messages must complete before it is possible to terminate. 
				
		DrainMailbox();
		
		return Served;
	}
	
	// The Count function simply returns the number of messages in the buffer.
	// This read operation should not necessitate a lock.
	
	inline MessageCount Count( void )
	{ return MessageBuffer.size(); }
	
	// The reset function just clears the message buffer. However since the queue
	// has no clear function, the elements must be popped one by one off the queue
	
	inline void Reset( void )
	{
		std::lock_guard< std::mutex > Lock( BufferGuard );
		
		while ( ! MessageBuffer.empty() )
			MessageBuffer.pop();
	}
	
	// The wait function is a dual interface: It will first consume the number 
	// of messages in the buffer up to the minimum value of the given limit and 
	// the buffer size. Then it will wait for the remaining messages using the 
	// Actor's wait function.
	
	inline MessageCount Wait( MessageCount MessageLimit = 1 )
	{
		MessageCount MessagesToConsume = std::min( Count(), MessageLimit );
		
		if ( MessageLimit - MessagesToConsume > 0 )
		{
			// There are more messages to be awaited than currently in the buffer, 
			// and it is necessary to instruct the Enqueue Message function to deliver 
			// messages directly to the mailbox to be immediately processed and 
			// counted by the actor's wait function.   
			
			Waiting = true;									
			
			Consume( MessagesToConsume );
		
			MessageCount AwaitedMessages =
															 Actor::Wait( MessageLimit - MessagesToConsume );
		  Waiting = false;
			return MessagesToConsume + AwaitedMessages;
		}
		else
		{
			// It is not possible to Consume the messages before the If-block because
			// the wait flag should be set before the consumption of buffered 
			// messages if a wait on new arriving messages is necessary. 
			
			Consume( MessagesToConsume );
			return MessagesToConsume;
		}
	}
	
	// The receiver has two constructors, one without argument that will give 
	// the receiver a default ActorNN name, and one for which a name can be 
	// given, but also an endpoint reference is needed. 
	
	Receiver( void )
	: Actor(), MessageBuffer(), BufferGuard()
	{ }
	
	Receiver( EndPoint & endPoint, const char *const name = 0 )
	: Actor( name ), MessageBuffer(), BufferGuard()
	{ }
};

}				// Name space Theron
#endif  // THERON_REPLACEMENT_ACTOR
