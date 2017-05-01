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
//#include <set>							  // To keep track of actor addresses
#include <functional>					// For defining handler functions
#include <list>								// The list of message handlers
#include <algorithm>					// Various container related utilities
#include <thread>						  // To execute actors
#include <condition_variable> // To synchronise threads
#include <atomic>							// Thread protected variables
#include <stdexcept>				  // To throw standard exceptions
#include <sstream>						// To provide nice exception messages

#include <iostream>					  // For debugging

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

// The remote identity of an actor is basically a string, and the actor 
// pointer is set to the local actor that has registered as the Presentation 
// Layer. The presentation layer is responsible for serialising and 
// de-serialising message that can then be transmitted as text strings to 
// remote network endpoints. This actor type must be forward declared.

class PresentationLayer;

// In this implementation everything is an actor as there is no compelling 
// reason for the other Theron classes. 

class Actor
{
/*=============================================================================

 Actor identification

=============================================================================*/
//
// Addressing an actor in Theron requires an address object, which can exist
// with or without the existence of the actor it refers to, and the address is 
// resolved to an actor only when a message is sent to this actor. Invalid 
// addresses are only indicated by the negative result of a message sending. 
// There is no way for an actor to detect a priori that an address is invalid, 
// and as such the actor address is just like an encapsulated string. Hence, 
// there must be an address class (to be defined below).

public:
class Address;

// The consequence of this model is that addresses can be used, for instance 
// as keys in lookup tables, and they can outlive the actor addressed. At the 
// same time one should be able to look up address by name (string) or by
// numerical ID. In order to ensure transparent actor communication, i.e. the 
// actor sending a message should not need to know if the receiver is a local 
// actor or a remote actor, the address does not need to point to a physical 
// memory location of a local actor.
//
// Furthermore, there are temporal aspects. An actor can be known by its name 
// before it is created, making it logically indistinguishable from a remote 
// actor. Then when the actor is created it should no longer be taken as a 
// possible remote actor, but as a know local actor. This implies that the 
// remote actor address either should be transformed to a local object, or 
// be removed as a known entity the moment no addresses reference the remote 
// actor any more. 
//
// The consequence of the transformation property is indirection: The address 
// cannot refer directly to the actor address, but to another object that knows
// the actor address, and can take the right message routing decisions depending
// on whether the actor is local, remote, or does not exist. The object 
// referenced by the addresses will here be called an actor Identification.
//
// This Identification should only exist as long as there is an address 
// referring to it. This could be implemented with some kind of registration 
// process where an address register with the Identification object when it is 
// created, and then de-register when the address is deleted. The benefit of 
// this approach is that the Identification object knows which addresses that 
// refers to it. The downside is that this is difficult to implement in a 
// multi-threaded implementation since one thread could delete an address 
// referring to the Identification and if it is the last address referring  
// to this Identification, it should delete the Identification if it is an
// Identification of a remote object. However, if another thread at the very 
// same time creates an address referring to the same Identification, it should
// not be deleted. Even if a mutex is used to guarantee that only one address 
// can register or de-register at the same time, the order of these two 
// operations cannot be predicted and the Identification object can be deleted
// when the next address tries to register. 
// 
// A simpler implementation, implicitly doing the same registration and 
// de-registration would be to consider the addresses as smart pointer to the 
// Identification. The Identification object will not know the address objects 
// referring to it, but when the last address is removed, the Identification 
// object's destructor would be called. A local actor can then just have an 
// address to itself, pointing to its Identification object, and ensuring that 
// this Identification object is removed when the actor is deleted.
//
// This is a cleaner implementation, but it does not overcome the problem of 
// simultaneous deletion and creation of the addresses referring to the object.
// Another form of indirection would come to our rescue: There must be a way 
// to refer to an actor, local or remote, by its string name or its numerical 
// ID. Hence there must be two lookup tables with references to the 
// Identification object. When the object is created, the references must be 
// inserted in these lookup tables, and when the object is deleted the 
// references must be deleted. Hence, having a mutex for insertions and 
// deletions in this lookup table will work. If the deletion event happens 
// first, the Identification will be deleted, and then when the creation event
// gets the mutex it will not find any Identification for the ID or the named 
// string and assumed it will be a new, remote (or future local) actor and 
// create another Identification object. If the creation event is handled first,
// then there will be an address holding a reference to the Identification 
// object so the deletion of the other address will just decrement the count of
// references.
//
// -----------------------------------------------------------------------------
// Identification
// -----------------------------------------------------------------------------
//
// An actor has a name and a numerical ID. The latter must be unique independent
// of how many actors that are created and destroyed over the lifetime of the 
// application. The Identification object is charged with obtaining the 
// numerical ID and store the name and the registration of the Identification
// during the actor's lifetime.
//
// Since an address is a smart pointer to the Identification object, it is 
// necessary to be able to create the smart pointer from the object itself, and 
// for this it should inherit a particular base class.

private:
class Identification : public std::enable_shared_from_this< Identification >
{
public:
	
	// The Numerical ID of an actor must be long enough to hold the counter of all 
	// actors created during the application's lifetime. 
	
	using IDType = unsigned long;

	// It also maintains the actor name and the actual ID assigned to this 
	// actor. Note that these have to be constant for the full duration of the 
	// actor's lifetime

	const IDType 			NumericalID;
	const std::string Name;
	
private:

	// It maintains a global counter which should be long enough to ensure that 
	// there are enough IDs for actors. This is incremented by each actor to 
	// avoid that any two actors get the same ID
	
	static std::atomic< IDType > TotalActorsCreated;
	
	// There is a small function to increment the total number of actors and 
	// return that id
	
	inline static IDType GetNewID( void )
	{	return ++TotalActorsCreated;	}
	
	// There is a pointer to the actor for which this is the ID. This 
	// will be the real actor on this endpoint, and for actors on remote 
	// endpoints, it will be the Presentation Layer actor which is responsible for 
	// the serialisation and forwarding of the message to the remote actor. 
	// This pointer is Null if no Presentation Layer is registered. Note the 
	// precedence: The Identification object is created when the first address 
	// object is created for a named actor. If no Presentation Layer is registered
	// the pointer becomes Null, otherwise it will point to the Presentation 
	// Layer actor. If then an actor with the same name is created, the actor 
	// pointer will be set to point to this local actor. When the local actor is 
	// deleted, this pointer is set to Null as all addresses referencing this 
	// Identification object must be removed before a new Identification object 
	// can be created for a remote actor of the same name. 

	Actor * ActorPointer; 
	
	// It will also keep track of the known actors, both by name and by ID.
	// All actors must have an identification, locally or remotely. It may be 
	// necessary to look up actors based on their name, and the best way
	// to do this is using an unordered map since a lookup should be O(1). 
	// These maps are protected since it is the responsibility of the derived
	// identity classes to ensure that the name and ID is correctly stored.
		
	static std::unordered_map< std::string, Identification * > ActorsByName;
	
	// In the same way there is a lookup map to find the pointer based on the 
	// actor's ID. This could have been a vector, but it would have been ever 
	// growing. Having a map allows the storage of only active actors.
	
	static std::unordered_map< IDType, Identification * > ActorsByID;
	
	// Since these three elements are shared among all actors, and actors can 
	// be created by other actors in the message handlers or otherwise, the
	// elements can be operated upon from different threads. It is therefore 
	// necessary to ensure sequential access by locking a mutex.
	
	static std::recursive_mutex InformationAccess;
	
  // An address is stored for the presentation layer so that messages for 
	// remote actors can be routed to the presentation layer for serialisation 
	// and remote transmission.

	static Address ThePresentationLayerServer;
	
	// The constructor is private and takes a name string only, and assigns a 
	// unique ID to the Identification object. The reason for keeping it private
	// is to ensure that the generator function is used when creating an 
	// identification object. If the name string is not given a default name 
	// "ActorNN" will be assigned where the NN is the number of the actor. It 
	// eventually register the identification object in the two lookups.
	
	Identification( const std::string & ActorName = std::string() );
	
	// The smart pointer to this Identification can be created with a simple 
	// support function. Even though it is now fundamentally just a renaming of 
	// the standard mechanism, it does allow flexibility for the future where 
	// the function could be made virtual to obtain derived identities.
	
	inline std::shared_ptr< Identification > GetSmartPointer( void )
	{	return shared_from_this();	}
			
public:	

	// Identification objects must be created by a supporting generator function
	// that checks that there is no Identification object already defined. In 
	// this case it will just return an address (reference) to that 
	// Identification object, after potentially setting its actor pointer to 
	// the actor. The last behaviour is needed in the case that an Identification
	// is first created to an actor by name, and then the actual actor is 
	// constructed later. Several addresses my then refer to the actor-by-name
	// (as a remote actor) already and the integrity of these must be kept. Hence
	// a new Identification object cannot be created, which is why there is no 
	// public constructor.
	
	static Address Create( const std::string & ActorName = std::string(), 
												 Actor * const TheActor = nullptr );
	
	// There are static functions to obtain an address by name or by numerical 
	// ID. 
	
	static Address Lookup( const std::string & ActorName );
	static Address Lookup( const IDType TheID );
	
	// Other actors may need to obtain a pointer to this actor, and this can 
	// only be done through a legal address object. It is not declared in-line 
	// since it uses the internals of the actor definition. This function will 
	// throw invalid_argument if the address is not for a valid and running actor.
	
	static Actor * GetActor( const Address & ActorAddress );
	
	// There is a static function used by the actor's destructor to clear the 
	// actor pointer.
	
	static void ClearActor( const Address & ActorAddress );
	
	// There is a simple test to see if the actor pointer is defined. Note that 
	// the actor pointer is only defined for actors on the local endpoint so this 
	// is implicitly a test that the actor is not remote.
	
	inline bool HasActor( void )
	{	return ActorPointer != nullptr;	}
	
	// A message can be routed to an actor identified by this Identification 
	// object only if it is a local actor, i.e. the actor pointer is set, OR 
	// the presentation layer is set. It is not in-line because it uses the 
	// address implementation to know if the presentation layer is valid.
	
	bool AllowRouting( void );

	// A static function is provided to set the presentation layer address
	
	static void SetPresentationLayerServer( const PresentationLayer * TheSever );
		
	// It is a recurring problem to keep main() alive until all actors have done 
	// their work. The following function can be called to all actors have empty 
	// queues and no running message handlers. It should then be safe to close the 
	// application

	static void WaitForGlobalTermination( void );

	// It is not possible to copy an Identification, or copy assign an 
	// Identification object.
	
	Identification( const Identification & OtherID ) = delete;
	Identification & operator = ( const Identification & OtherID ) = delete;
	
	// The destructor removes the named actor and the ID from the registries, 
	// and since it implies a structural change of the maps, the lock must be 
	// acquired. It is automatically released when the destructor terminates.

	~Identification( void );
};

/*=============================================================================

 Address management

=============================================================================*/

// The main address of an actor is it physical memory location. This can be 
// reached only through an address object that has a pointer to the actor 
// Identification of the relevant actor. From the previous discussion it may 
// be that the Identification fails to point at a real actor, and there is 
// no guarantee that a message can be sent to an address - it should be checked
// first, otherwise the send function may throw (see below).
	
public:
	
class Address : protected std::shared_ptr< Identification >
{
private:
	
	// There is a standard constructor from another shared Identification pointer
	
	Address( std::shared_ptr< Identification > & OtherAddress )
	: std::shared_ptr< Identification >( OtherAddress )
	{ }
	
	// A similar constructor is needed to move the other address pointer if it 
	// is assigned as a temporary variable.

	Address( std::shared_ptr< Identification > && OtherAddress )
	: std::shared_ptr< Identification >( OtherAddress )
	{ }
	
	// The identification class is allowed to use the shared pointer constructors
	
	friend class Identification;	
		
public:
	
	// The copy constructor is currently just doing the same as the shared 
	// pointer constructor since the address has now own data fields to be copied
	
	inline Address( const Address & OtherAddress )
	: std::shared_ptr< Identification >( OtherAddress )
	{	}
	
	// The move constructor is similarly trivial
	
	inline Address( Address && OtherAddress )
	: std::shared_ptr< Identification >( OtherAddress )
	{ }

	// The void constructor simply default initialises the pointer
	
  inline Address( void )
	: std::shared_ptr<Identification>()
	{}
		
  // There is constructor to find an address by name and it is suspected that 
	// this will be used also for the plain string defined by Theron 
	// (const char *const name). These will simply use the Actor identification's 
	// lookup, and then delegate to the above constructors.
	
	inline Address( const std::string & Name )
	: Address( Identification::Lookup( Name ) )
	{
		// The lookup may fail if no actor, either locally or remotely, exists by 
		// that name. In that case, it is understood that this is a reference to 
		// a remote actor, or to a local actor not yet created, and the 
		// corresponding address object is constructed and assigned to this 
		// address.
		
		if ( get() == nullptr )
			*this = Identification::Create( Name );
	}
	
	// Oddly enough, but Theron has no constructor to get an address by the 
	// numerical ID of the actor, however here one is provided now.
	
	inline Address( const Identification::IDType & ID )
	: Address( Identification::Lookup( ID ) )
	{	}	

	// There is an assignment operator that basically makes a copy of the 
	// other Address. However, since it is called on an already existing 
	// address, it must assign to its own shared pointer
	
	inline Address & operator= ( const Address & OtherAddress )
	{
		std::shared_ptr< Identification >::operator = ( OtherAddress );
		
		return *this;
	}
	
	// It should have a static function Null to allow test addresses
	
	static const Address Null( void )
	{	return Address();	}
	
	// There is an implicit conversion to a boolean to check if the address is 
	// valid or not. An address is valid if it points at an Identification object,
	// AND the Identification object can be used for routing.
	
	inline operator bool (void ) const
	{	return (get() != nullptr) && get()->AllowRouting();	}
	
	// There are comparison operators to allow the address to be used in standard
	// containers.
	
	inline bool operator == ( const Address & OtherAddress ) const
	{ return get() == OtherAddress.get(); }
	
	inline bool operator != ( const Address & OtherAddress ) const
	{ return get() != OtherAddress.get(); } 

	// The less-than operator is more interesting since the address could be 
	// Null and how does that compare with other addresses? It is here defined 
	// that a Null address will be larger than any other address since it is 
	// assumed that this operator defines sorting order in containers and then 
	// it makes sense if the Null operator comes last.
	
	bool operator < ( const Address & OtherAddress ) const
	{
		if ( get() == nullptr )
			return true;
		else 
			if ( OtherAddress.get() == nullptr ) return true;
		  else 
				return get()->NumericalID < OtherAddress->NumericalID;
	}
	
	// There is another function to check if a given address is a local actor. 
 
	inline bool IsLocalActor( void ) const
	{
		return ( get() != nullptr ) && ( get()->HasActor() );
	}
		
	// Then it must provide access to the actor's name and ID that can be 
	// taken from the actor's stored information. If these are called on a Null
	// address, they will throw a standard logic error.
	
	inline std::string AsString( void ) const
	{	
		if ( get() == nullptr )
			throw std::logic_error( "Null address has no name" );
	  
		return get()->Name;
	}
	
	inline Identification::IDType AsInteger( void ) const
	{	
		if ( get() == nullptr )
			throw std::logic_error( "Null address has no numerical ID");
		
		return get()->NumericalID;
	}
	
	inline Identification::IDType AsUInt64( void ) const
	{	return AsInteger();	}
	
	// There is a legacy function to obtain the numerical ID of the framework, 
	// which is here taken identical to the actor pointed to by this address. 
	// It will throw a logic error if it is a null address.
	
	inline Identification::IDType GetFramework( void ) const
	{
		if ( get() == nullptr )
			throw std::logic_error( "Null address has no framework!" );
		
	  return get()->NumericalID;
	}	
};
	
// The actor itself has its own ID as an address

private:
Address ActorID;

// The standard way of obtaining the address of an actor will then just return
// this address, which will be implicitly copied to some other address.

public:
inline Address GetAddress( void ) const
{	
	return ActorID; 
}

// There is a function to check if an address corresponds to a local actor. 
// The best would be to call the function on the address, but this is an 
// indirect way of doing the same. It is static since it can be called
// without having an actor. 

inline static bool IsLocalActor( const Address & RequestedActorID )
{
	return RequestedActorID.IsLocalActor();
}
	
// The Presentation layer address needed to support external communication can 
// be set with the a static function defined in the Identification class, but 
// the Identification class is and should be a private class of the Actor. An
// interface is therefore provided to allow the Presentation Layer to register 
// with this function.

inline static void SetPresentationLayerServer( 
																					const PresentationLayer * TheServer )
{ Identification::SetPresentationLayerServer( TheServer ); }
	
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
	
	inline GenericMessage( void )
	: From(), To()
	{ }
	
	// It is important to make this class polymorphic by having at least one 
	// virtual method, and it must in order to ensure proper destruction of the 
	// derived messages.
	
	virtual ~GenericMessage( void )
	{ }
};

// Each actor has a queue of messages and add new messages to the end and 
// consume from the front of the queue. It can therefore be implemented as a 
// standard queue. Other Actors will place messages for this actor into this
// queue, and this actor will consume messages from this queue. Hence, this 
// queue will be the a memory resource shared and accessed by several threads
// and it therefore needs proper mutex protection. The access to the standard
// queue is consequently encapsulated in a message queue class. It is a private
// class as no derived actor should need to directly invoke any of the provided
// methods.

private:

class MessageQueue : protected std::queue< std::shared_ptr< GenericMessage > >
{
private:
	
	// The messages queue has a mutex to serialise access to the queue, and it 
	// supports the notification of two events: One for the arrival of a new 
	// message and one for the completed message handling. One for ingress 
	// messages and one for egress.
	
	std::timed_mutex 						QueueGuard;
	std::condition_variable_any NewMessage, MessageDone;
	
	// An interesting aspect with waiting for these events is that the condition
	// triggering the event may have changed when the thread waiting for the event
	// is started. For instance, if one is waiting for a new message on an empty 
	// queue, this message could already be processed by the time the thread 
	// waiting for this signal gets an opportunity to run. Checking the size of 
	// the queue would then again yield an empty queue, and there should be no 
	// reason to terminate the wait. This condition would not happen if the 
	// following requirements are fulfilled by the thread implementation:
	//
	// 	1) A notifying all waiting threads will immediately make them ready to 
  //     run and they will all try to lock the mutex.
	//  2) Access to the mutex is given in order of request. In other words, 
	//     all the threads woken by the notification will have locked the mutex
	//     before the lock would again be acquired from this thread to add or 
	//     delete messages. 
	//
	// It has not been possible to find any documentation for this behaviour, 
	// although it is reasonable. 
		
public:
	
	// The fundamental operations is to store a message and to delete the first 
	// message of the queue. The two operations will signal the corresponding 
	// condition variable.
	
	void StoreMessage( const std::shared_ptr< GenericMessage > & TheMessage );
	void DeleteFirstMessage( void );
	
	// The owning actor will need to access the first message in the queue, and
	// since messages in the queue can only be deleted by the owning actor when 
	// the message has been handled, it is a safe operation to read the first 
	// element and the standard 'front' function for the queue is directly used
	
	using std::queue< std::shared_ptr< GenericMessage > >::front;
	
	// The size type of the queue is also allowed for external access to ensure
	// that other types are using the correct type
	
	using std::queue< std::shared_ptr< GenericMessage > >::size_type;
	
	// Reading the current size of the queue should be allowed
	
	using std::queue< std::shared_ptr< GenericMessage > >::size;
	
	// There is a function to check if the queue is empty, and to ensure the 
	// correctness of the test, it must prevent new messages from arriving while 
	// the test is performed. It can also be that one would like to wait for the 
	// next message if the queue is empty. It therefore supports two alternatives
	
	enum class QueueEmpty
	{
		Return,
		Wait
	};
	
	bool HasMessage( QueueEmpty Action = QueueEmpty::Return );
	
	// It could also be that one would like to wait for the next message to 
	// arrive. This function will therefore block the calling thread until the 
	// next message arrives and the new message condition is signalled. It 
	// optionally takes an address of a sender to wait for and if this is given
	// it will continue to wait until a message from that sender is received.
	// It is up to the application to ensure that this will not block forever in 
	// that case; or in the case there will never be another message for this 
	// actor.
	
	void WaitForNextMessage( const Address & SenderToWaitFor = Address::Null() );
	
	// Finally, there is a method to wait for the queue to become empty. Again,
	// this cannot be called from one of the actor's message handlers as that 
	// will create a deadlock, but it could be necessary for one actor to wait 
	// until another actor has processed all of its messages.
	
	void WaitUntilEmpty( void );
	
	// The constructor is simply a place holder for initialising the queue and
	// the locks
	
	MessageQueue( void )
	: std::queue< std::shared_ptr< GenericMessage > >(),
	  QueueGuard(), NewMessage(), MessageDone()
	{ }
	
	// The destructor does nothing special and the default destructor is good 
	// enough.
	
} Mailbox;

// The size type is defined as it is convenient to use for anything that has 
// to do with the message queue

protected:

using MessageCount = MessageQueue::size_type;

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
	: GenericMessage( From, To ), TheMessage( MessageCopy )
	{ }
	
	virtual ~Message( void )
	{ }
};

// Messages are queued a dedicated function that obtains a unique lock on the 
// queue guard before adding the message. The return type could be used to 
// indicate issues with the queuing, but the function should really throw
// an exception in case of serious errors. The function is virtual in order 
// to implement transparent communication. Theron's message handlers does not 
// contain the "To" address of a message because they are called on the 
// receiving actor. However, if the receiving actor is remote, the message 
// hander should be on the Presentation Layer for serialising the message for 
// network transfer. The "To" address needs to be a part of the serialised 
// message, and hence the Presentation Layer needs to catch the Generic 
// Message.

protected:
	
virtual
bool EnqueueMessage( const std::shared_ptr< GenericMessage > & TheMessage );

// There is a callback function invoked by the Postman when a new message has 
// been processed. It is virtual in order to allow derived classes to be 
// notified when a message has been processed.

protected:
	
virtual void MessageProcessed( void );

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
								  const Address & TheSender, const Address & TheReceiver )
{
	Actor * ReceivingActor = Identification::GetActor( TheReceiver );
	auto    MessageCopy    = std::make_shared< MessageType >( TheMessage );
	
	return	
	ReceivingActor->EnqueueMessage( std::make_shared< Message< MessageType> >(	
																	MessageCopy, TheSender, TheReceiver	));
}

// Theron's actor has a simplified version of the send function basically just 
// inserting its own address for the sender's address.

protected:
	
template< class MessageType >
inline bool Send( const MessageType & TheMessage, const Address & TheReceiver )
{
	return Send( TheMessage, GetAddress(), TheReceiver );
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
// both Theron defined variants of the default handler type.

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

// There is a simple flag indicting if the actor is running. It is set to true 
// in the Actor's constructor and to false in the destructor and it should 
// be checked in other threads waiting for this Actor in some way. It is marked
// as volatile to ensure that the Postman will load it from memory when it is 
// checked as it may have been changed by another thread closing the actor.

private: 
	
volatile bool ActorRunning;

// -----------------------------------------------------------------------------
// Message processing
// -----------------------------------------------------------------------------
//
// Execution of the handlers for queued messages will take place in a dedicated
// thread that will be started at construction time.

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

// -----------------------------------------------------------------------------
// Synchronisation
// -----------------------------------------------------------------------------
//
// In general, actors should proceed completely asynchronous. However, there is 
// a particular pattern that makes this difficult: request - response. If an 
// actor sends a request to another actor and expect a response there are two
// situations to consider: The response will not depend on the current actor 
// state, and the actor can therefore continue normal message processing until 
// the response arrived and is processed as a normal message. Alternatively,
// the actor should wait for the response to arrive, before processing any other
// message. This latter case is more difficult and requires some kind of 
// synchronisation.
//
// Again, there are two cases to consider: 1) The actual content of the 
// response has a meaning. In this case the actor should set up a Receiver and 
// send the request as if it was from this receiver, and wait for the receiver
// to receive and handle the response before the necessary response content 
// information is obtained from the receiver. Looking at the stack this means 
// that the Postman runs a message handler, which blocks on a receiver. 
// consequently no more messages will be processed by the actor during this 
// wait, but they will be queued up. The response arriving to the Receiver 
// will be processed as soon as it arrives since the Receiver is an actor that 
// has its own Postman. Conceptually, this implies that the response message 
// 'bypasses' the other messages to the actor and will be processed before the 
// other messages in the actor's mailbox. The Receiver provides the necessary 
// mechanism for implementing this, and it is the standard way to manage 
// responses. 
//
// 2) In this case the response is a simple acknowledgement from the other actor
// that the request has been received, and potentially processed. The sole fact
// that a message has arrived from the other actor is sufficient for this 
// actor to continue processing, and then handle the acknowledgement at some 
// later time according to the place of the message in the queue. Also this 
// case can be implemented with a Receiver, although it would be better if the 
// message handler could wait until a message arrives. To support this 
// scenario, a special function is provided to wait for the arriving message, 
// and the function terminates with the address of the sender of the last 
// message in the queue to allow the waiting message handler to check if this 
// was the right sender and potentially restart the wait if it was not.

protected:

inline 
void WaitForNextMessage( const Address & AddressToWaitFor = Address::Null() )
{ 
	Mailbox.WaitForNextMessage( AddressToWaitFor ); 
}

// There is a small helper function that waits for the Postman to complete the 
// delivery of messages, i.e. wait for the mailbox to drain. Essentially, it 
// is just a wait until the message queue is empty. This method cannot be 
// called from the actor itself, as it would cause the same type of deadlock 
// as for the previous wait function, and it will throw a logic error if it is 
// called from the Postman. It needs a condition variable to indicate when the 
// queue is empty. Since it is related to the queue, it is protected by the 
// queue's mutex Queue Guard.

public:
void DrainMailbox( void );

// The next level is to use the drain mailbox wait on all running actors. This
// is delegated to the Identification's static wait function that will loop 
// over all local actors waiting for them to drain the mailbox until there are
// no more actors with messages. This can typically be used to block the main 
// thread until the whole actor system has finished processing.

inline static void WaitForGlobalTermination( void )
{
	Identification::WaitForGlobalTermination();
}

// Waiting for a message to be processed is provided via the virtual call back 
// function Message Processed, and the Receiver class below provides a way for
// another thread to wait for an actor to receive and process one or more 
// messages. The philosophical problem with the Receiver is that another thread
// or actor directly calls a method on the Receiver and thereby breaking the 
// actor model. It is better to implement actor synchronisation by a message 
// protocol, although it may not always be possible because one could need to
// wait for an acknowledgement to be returned before continuing with the 
// processing of messages. In this case, the current message handler should 
// block waiting for the acknowledgement, and creating a Receiver and blocking 
// on that Receiver could be a more robust alternative than to use the Wait For 
// the Next Message function above.

/*=============================================================================

 Constructors and destructor

=============================================================================*/

// The actor allows the a user defined name to be given, and if it is omitted 
// it will be assigned by default as "ActorNN" where NN is the numerical ID of 
// the actor.

inline Actor( const std::string & ActorName = std::string() )
: ActorID( Identification::Create( ActorName, this ) ), 
  Mailbox(), MessageHandlers(), DefaultHandler(), Postman()
{
	// The flag indicating if the actor is running is set to true, and currently
	// there is no message available.
	
	ActorRunning = true;
	
	// The default error handling policy is to throw on unhanded messages
	
	MessageErrorPolicy = MessageError::Throw;
	
	// Then the thread can be started. It will wait until it is signalled from 
	// the Enqueue message function. 
	
	Postman = std::thread( &Actor::DispatchMessages, this );	
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
// consume message or the wait message. Looking at the actual receiver 
// implementation reveals that the receiver just maintains a counter of arrived
// messages, and process them as they arrive. The counter is decreased by the 
// consume and the wait method, and the actual wait only happens if the counter
// reaches zero. The current implementation implements this behaviour.

class Receiver : public Actor
{
private:
	
	// There is a counter for messages that has arrived an not yet Consumed or 
	// waited for.
	
	volatile MessageCount Unconsumed;
	
	// The actual wait is ensured by a condition variable and a mutex to wait 
	// on this variable

	std::condition_variable_any OneMessageArrived;
  std::timed_mutex						WaitGuard;
	
protected:
	
	// The main hook for the receiver functionality in extending the functionality
	// of the actor is the new message function. This will add to the count of 
	// unconsumed messages and notify the wait handler.
	
	virtual void MessageProcessed( void )
	{
		std::unique_lock< std::timed_mutex > Lock( WaitGuard, 
																							 std::chrono::seconds(10) );
		Unconsumed++;
		OneMessageArrived.notify_one();
	}
	
public:
	
	// The Consume function takes a number of messages up to the message limit 
	// or to the end of the buffer and hands these over to the Actor's enqueue 
	// function for processing. After passing on the messages it will wait for 
	// the Postman to complete the processing of these messages before terminating
	// with the number of messages that could be processed.
	
	inline MessageCount Consume( MessageCount MessageLimit )
	{
		std::unique_lock< std::timed_mutex > Lock( WaitGuard, 
																							 std::chrono::seconds(10) );
		
		MessageCount Served = std::min( static_cast< MessageCount >( Unconsumed ), 
																		MessageLimit );
		
		Unconsumed -= Served;
		
		return Served;
	}
	
	// The Count function simply returns the number of messages still unconsumed
	// by calls to the counter or the wait function.
	
	inline MessageCount Count( void )
	{ return Unconsumed; }
	
	// The reset function just clears the counter.
	
	inline void Reset( void )
	{
		std::lock_guard< std::timed_mutex > Lock( WaitGuard );
		
		Unconsumed = 0;
	}
	
	// The wait function in Theron only blocks if the there are no unconsumed 
	// messages. If there are messages to consume, it will just return the number
	// of messages to consume up to the maximum limit. It means that calling the 
	// Wait function with an argument of say, 4, with 3 messages unconsumed will 
	// make the function return 3 and not wait for the forth message. This 
	// behaviour seems strange, but since the whole purpose of this implementation
	// is to copy the Theron Receiver, the same behaviour is implemented here.
	
	inline MessageCount Wait( MessageCount MessageLimit = 1 )
	{

		std::unique_lock< std::timed_mutex > Lock( WaitGuard, 
																							 std::chrono::seconds(10) );

		// The wait function will wait for one message. It will not block if there 
		// are already unconsumed messages.

		OneMessageArrived.wait( Lock, [&](void)->bool{ return Unconsumed > 0; });
		
		// Theoretically, more than one message could have arrived from the 
		// point in time when the Wait function is called until the return of 
		// the function. The Consume function can be called on its own and it will
		// therefore lock, which means that the lock should be released prior to 
		// this call.
		
		Lock.unlock();

		return Consume( MessageLimit );
	}
	
	// The receiver has two constructors, one without argument that will give 
	// the receiver a default ActorNN name, and one for which a name can be 
	// given, but also an endpoint reference is needed. 
	
	Receiver( void )
	: Actor(), Unconsumed(0), OneMessageArrived(), WaitGuard()
	{ }
	
	Receiver( EndPoint & endPoint, const char *const name = 0 )
	: Actor( name ), Unconsumed(0), OneMessageArrived(), WaitGuard()
	{ }
};

}				// Name space Theron
#endif  // THERON_REPLACEMENT_ACTOR
