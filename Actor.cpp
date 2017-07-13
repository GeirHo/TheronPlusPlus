/*=============================================================================
Actor

Author and Copyright: Geir Horn, 2017
License: LGPL 3.0
=============================================================================*/

#include <chrono>									// To support time out mutexes
#include <functional>							// Run-time functions

#include "Actor.hpp"							// The Actor definition
#include "PresentationLayer.hpp"  // The Presentation Layer definition

/*=============================================================================

 Actor identification

=============================================================================*/

// Static members shared by all actors, but protected within the Identification 
// object. 

std::atomic< Theron::Actor::Identification::IDType >
				Theron::Actor::Identification::TotalActorsCreated(0);

std::unordered_map< std::string, Theron::Actor::Identification * >
				Theron::Actor::Identification::ActorsByName;
				
std::unordered_map< Theron::Actor::Identification::IDType, 
										Theron::Actor::Identification * >
				Theron::Actor::Identification::ActorsByID;

std::recursive_mutex Theron::Actor::Identification::InformationAccess;

Theron::Actor::Address 
Theron::Actor::Identification::ThePresentationLayerServer;

// For compatibility reasons there is a global framework pointer

Theron::Framework * Theron::Actor::GlobalFramework = nullptr;

// -----------------------------------------------------------------------------
// Static functions 
// -----------------------------------------------------------------------------
//
// The presentation layer server can only be set by a presentation layer to 
// its address. It will throw a logical error exception if the presentation 
// layer server already exists

void Theron::Actor::Identification::SetPresentationLayerServer( 
																		const Theron::PresentationLayer * TheSever )
{
	if ( ThePresentationLayerServer )
	{
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
							   <<  "Only one presentation server can be used!"
								 << " Already set to " << ThePresentationLayerServer.AsString();
 
		throw std::logic_error( ErrorMessage.str() );
	}
	
	ThePresentationLayerServer = TheSever->GetAddress();
}

// Creating an Identification object is subject to an initial verification that
// an object with the given name does not already exist. If it does, an address
// to that object is returned, possibly with the actor pointer set to the value
// of the pointer given (which is the case when this function is used from an 
// actor's constructor)

Theron::Actor::Address Theron::Actor::Identification::Create(
								 const std::string & ActorName, Theron::Actor * const TheActor )
{
	// In order to ensure that the Identification object of an existing actor is 
	// not destructed in parallel with the creation of a reference to the same 
	// object, access to the maps must be locked.
	
	std::lock_guard< std::recursive_mutex > Lock( InformationAccess );
	
	// The found Identification object is stored. There is a very subtle learning
	// point: This could have been a raw pointer, but that would leave the 
	// shared pointer of the base class of the identification void. The shared 
	// pointer constructor ensures that it is possible to say 'enable shared from 
	// this', and so a shared pointer must be set to take care of the newly
	// created Identification object. Furthermore, since the share pointer 
	// constructor updates the 'enable shared from this' base class, that base 
	// class could not be protected.
	
	std::shared_ptr< Identification > TheActorID;
	
	// The lookup is considered first: If the name is not given, then a new ID 
	// with automatically assigned name must be created, otherwise a check is
	// made to find an actor with the given name. If no actor is found, then a 
	// new Identification is again constructed. Note that the constructor of the 
	// Identification object takes care of inserting the Identification object 
	// in the two lookup maps.
	
	if ( ActorName.empty() )
    TheActorID = std::shared_ptr< Identification >( new Identification() );
	else
  {
		auto ExistingActor = ActorsByName.find( ActorName );
		
		if ( ExistingActor == ActorsByName.end() )
			TheActorID = std::shared_ptr< Identification >( 
																							new Identification( ActorName ) );
		else
			TheActorID = ExistingActor->second->GetSmartPointer();
	}
	
	// An actor is allowed to steal the Actor Pointer if this actor pointer is 
	// either unassigned or set to the Presentation Layer actor indicating that 
	// this Identification could be for a remote object which is now confirmed 
	// to be a local identity. If no actor pointer is given, the the Actor Pointer
	// of the Identification object is set to the Presentation Layer server if 
	// that is known. Otherwise, if an actor pointer is given to this function 
	// for a named actor that is already set to another actor that is not the 
	// Presentation Layer actor, there is a serious logical error in the 
	// application and an exception will be thrown.
	
	if ( TheActor != nullptr )
  {
		if ((TheActorID->ActorPointer == nullptr) ||
				(TheActorID->ActorPointer == ThePresentationLayerServer->ActorPointer) )
			TheActorID->ActorPointer = TheActor;
		else if( TheActor != TheActorID->ActorPointer )
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "Attempt to Create an Identification with name "
									 << ActorName << " and address " << TheActor 
									 << " but this named Identification already points "
									 << " to the actor at " << TheActorID->ActorPointer;
									 
		  throw std::logic_error( ErrorMessage.str() );
		}
	}
	else if ( ThePresentationLayerServer->ActorPointer != nullptr )
		TheActorID->ActorPointer = ThePresentationLayerServer->ActorPointer;
	
	// At this point the Identification pointer is correctly set, and the 
	// address constructed from its shared pointer can be returned.
	
	return Address( TheActorID );
}

// The static function to return the actor pointer based on a an address will
// throw an invalid argument if the given address is Null

Theron::Actor * Theron::Actor::Identification::GetActor( 
																	 const Theron::Actor::Address & ActorAddress )
{
	std::lock_guard< std::recursive_mutex > Lock( InformationAccess );
	
	if ( ActorAddress )
  {
		Actor * TheActor = ActorAddress->ActorPointer;
		if ( TheActor != nullptr )
			return TheActor;
		else
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "GetActor: NULL actor pointer";
			
			throw std::invalid_argument( ErrorMessage.str() );
		}
  }
	else
	{
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << "GetActor: NULL actor address";
								 
		throw std::invalid_argument( ErrorMessage.str() );
	}
		
}

// The first lookup function by name simply acquires the lock and then 
// constructs the address based on the outcome of this lookup. Note that this 
// may result in an invalid address if the given actor name is not found.

Theron::Actor::Address Theron::Actor::Identification::Lookup(
																								 const std::string & ActorName )
{
	std::lock_guard< std::recursive_mutex > Lock( InformationAccess );
	
	auto TheActor = ActorsByName.find( ActorName );
	
	if ( TheActor == ActorsByName.end() )
		return Address();
	else
		return TheActor->second->GetSmartPointer();
}

// The second lookup function is almost identical except that it uses the ID 
// map to find the actor.

Theron::Actor::Address Theron::Actor::Identification::Lookup(
																														const IDType TheID )
{
	std::lock_guard< std::recursive_mutex > Lock( InformationAccess );
	
	auto TheActor = ActorsByID.find( TheID );
	
	if ( TheActor == ActorsByID.end() )
		return Address();
	else
		return TheActor->second->GetSmartPointer();
}

// Clearing the actor is just setting the actor pointer to the null pointer 
// effectively invalidating all addresses pointing to this Identification 
// object ensuring that no further messages will be sent to this actor. If 
// the actor is meant to re-appear on a remote endpoint, which is bad practice,
// it can only be re-created as an Identification object on this endpoint after
// all addresses referring to the Identification have been cleared, and the 
// Identification object has been deleted.
// 
// However, if the actor closing is currently registered as the Presentation 
// Layer, it could have several addresses pointing at it, and it must therefore
// clear multiple pointers.

void Theron::Actor::Identification::ClearActor( 
																	const Theron::Actor::Address & ActorAddress )
{
	std::lock_guard< std::recursive_mutex > Lock( InformationAccess );
	
	if ( ActorAddress == ThePresentationLayerServer )
  {
		for ( auto & TheID : ActorsByID )
			if ( TheID.second->ActorPointer == ActorAddress->ActorPointer )
				TheID.second->ActorPointer = nullptr;
			
		ThePresentationLayerServer = Address::Null();
	}
	else
		ActorAddress->ActorPointer = nullptr;
}

// -----------------------------------------------------------------------------
// Other functions
// -----------------------------------------------------------------------------
//
// A small check to see if the Identification object can be used for routing 
// messages. Note that the default validity check on addresses cannot be used 
// for the Presentation Layer Server Pointer because this check will call the 
// Allow Routing function which would lead to an infinite recursion. Instead 
// the address is compared against the Null address to verify that the pointer 
// is properly set.

bool Theron::Actor::Identification::AllowRouting( void )
{
	return ( ActorPointer != nullptr ) || 
				 ( ( ThePresentationLayerServer != Address::Null() ) && 
				   ( ThePresentationLayerServer->ActorPointer != nullptr ) ) ;
}

// -----------------------------------------------------------------------------
// Constructor and Destructor
// -----------------------------------------------------------------------------
//
// Then this newly created Identification object is inserted into the 
// relevant maps. The access to the maps must be locked to ensure to prevent 
// the situation where an Identification is created and destroyed at the same 
// time

Theron::Actor::Identification::Identification( const std::string & ActorName )
: enable_shared_from_this(), 
  NumericalID( GetNewID() ),  Name( ActorName.empty() ? 
		  "Actor" + std::to_string( NumericalID ) : ActorName ),
  ActorPointer( nullptr )
{	
	std::lock_guard< std::recursive_mutex > Lock( InformationAccess );
	
	ActorsByName.emplace( Name,        this );
	ActorsByID.emplace  ( NumericalID, this );
}

// The destructor simply reverses this registration after locking the access
// to the registries.

Theron::Actor::Identification::~Identification( void )
{
  std::lock_guard< std::recursive_mutex > Lock( InformationAccess )	;
	
	ActorsByName.erase( Name 			  );
	ActorsByID.erase  ( NumericalID );
}


/*=============================================================================

 Message handling

=============================================================================*/

// The mailbox stores a message by simply enqueue it after locking the mutex 
// to ensure that only one thread will enqueue a message at the time. After 
// adding the message to the queue it will signal the new message condition.

void Theron::Actor::MessageQueue::StoreMessage(
													  const std::shared_ptr<GenericMessage> & TheMessage )
{
	std::unique_lock< std::mutex > QueueLock( QueueGuard );
	push( TheMessage );
	NewMessage.notify_all();
}

// When an actor sends a message to another actor, it will call the 
// enqueue message function on the receiving agent with a pointer to a copy of
// the message to ensure that it does exists also when the message is handed 
// by the receiving actor. The enqueue function will simply ask the mailbox to
// store the message.
//
// The enqueue function will first append the message to the message queue, and 
// if no postman in running, it will start the thread to dispatch the message 
// to the right handler.

bool Theron::Actor::EnqueueMessage( 
													const	std::shared_ptr< GenericMessage > & TheMessage )
{
	Mailbox.StoreMessage( TheMessage );
	return true;
}

// A message is deleted from the dispatching function after it has been given 
// to all message handlers for this message type. It indicates the end of the 
// processing for this message so it can safely be popped from the queue and 
// other threads waiting for this event can be notified.

void Theron::Actor::MessageQueue::DeleteFirstMessage( void )
{
	std::unique_lock< std::mutex > QueueLock( QueueGuard );
	pop();
	MessageDone.notify_all();
}

// The method to check if the queue is empty will wait for a message if that 
// is the selected policy. This is safe to use from the methods running under 
// the Postman like its dispatch method or message handlers since the arrival 
// events will be created by other actors, i.e. other threads.

bool Theron::Actor::MessageQueue::HasMessage(
																Theron::Actor::MessageQueue::QueueEmpty Action )
{
	std::unique_lock< std::mutex > QueueLock( QueueGuard );
	if ( empty() )
  {
		if ( Action == QueueEmpty::Wait )
		{
			NewMessage.wait( QueueLock, [&](void)->bool{ return size() > 0; } );
			return true;
		}
		else
			return false;
	}
	else
		return true;
}

// Waiting for the next message to arrive is a forced version of the check for 
// message function since it will block until the message has arrived. This is
// therefore also safe to be used from message handlers via the corresponding 
// function on the actor's interface.

void Theron::Actor::MessageQueue::WaitForNextMessage(
																const Theron::Actor::Address & SenderToWaitFor )
{
	// The lock is taken and the current queue size is remembered
	
	std::unique_lock< std::mutex > QueueLock( QueueGuard );
	auto IngressQueueSize = size();
	
	// The condition depends on what the given address is. If it is Null then 
	// the wait should terminate whenever there is a new message. If an address 
	// is given the wait should terminate when there the newly received message 
	// is from the specified sender. 
	
	if ( SenderToWaitFor == Address::Null() )
		NewMessage.wait( QueueLock, 
										 [&](void)->bool{ return size() > IngressQueueSize; } );
	else
		NewMessage.wait( QueueLock, 
										 [&](void)->bool{ return back()->From == SenderToWaitFor;});	
}

// Waiting for the the queue to drain is potentially the only wait that could 
// have multiple threads waiting for the signal since the condition for 
// termination is unique: The queue should be empty! If another message has 
// arrived before a thread manages to start up and process the notification,
// then it is still right to block again waiting for the next signal.

void Theron::Actor::MessageQueue::WaitUntilEmpty( void )
{
	std::unique_lock< std::mutex > QueueLock( QueueGuard );

	// Since the condition is tested before the wait triggers, it is safe to call
	// the wait directly even if the queue should already be empty.
	
  MessageDone.wait( QueueLock, [&](void)->bool{ return empty(); } );	
}

/*=============================================================================

 Execution control

=============================================================================*/

// The dispatcher function first obtains a copy of the message at the front of 
// the message queue, and then tries to deliver this to each handler in turn
// Handlers successfully managing this message will be moved forward in the 
// list of handlers according to the transposition rule suggested by Ronald 
// Rivest (1976): "On self-organizing sequential search heuristics", 
// Communications of the ACM, Vol. 19, No. 2, pp. 63-67
//
// If no message handler is available to serve the message the message will 
// be delivered to the default message handler. If no default message handler
// exists, then the message will be handled according to the error policy set.
//
// The handler is supposed to run in the actor's thread and it will block if 
// there is no message available. 

void Theron::Actor::DispatchMessages( void )
{
	// Messages are continuously dispatched as long as there are more messages 
	// and the actor is running. Note that the order of these tests is important 
	// when the actor terminates because the destructor will first wait for the 
	// queue to drain, and then set the running flag to false before submitting 
	// an empty message that will stop the wait on a message, but cause the while 
	// loop to terminate because the actor is no longer running and thereby avoid 
	// processing the empty message.
	
	while ( Mailbox.HasMessage( MessageQueue::QueueEmpty::Wait ) && ActorRunning )
  {
	  // There is an iterator to the current handler, and a flag indicating that 
		// the message has been served by at least one handler.
		
		auto CurrentHandler = MessageHandlers.begin();
		bool MessageServed  = false;
		
		// It is an overhead to call Mailbox front for each handler as the first 
		// message in the queue can be cached
		
		auto TheMessage = Mailbox.front();
		
		// ...and then loop over all handlers to allow them to manage the message 
		// if they are able to.
		
		while ( CurrentHandler != MessageHandlers.end() )
		{
			// There is a minor problem related to the handler call since invoking the 
			// message handler may create or destroy handlers. A mutex cannot help 
			// since the handler is executing in this thread, and since it runs on 
			// the same stack all operations implicitly made by the handler
			// on the handler list will have terminated when control is returned to 
			// this method. Insertions are not problematic since they will appear at 
			// the end of the list, and will just be included in the continued 
			// iterations here. Deletions are similarly not problematic unless the 
			// handler de-register itself. 
			//
			// In this case it does not help having an iterator to the next element 
			// as there is also no guarantee that that also that pointer will not be 
			// deleted. The only safe way is to ensure that the handler object for 
			// the current handler is not deleted. A copy of the current handler is 
			// therefore made, and its status is set to executing.
			
			auto ExecutingHandler = CurrentHandler;
			(*ExecutingHandler)->SetStatus( GenericHandler::State::Executing );
			
			// Then the handler can process the message, and if this results in the 
			// handler de-registering this handler, it will return with the deleted 
			// state.
						
			if( (*CurrentHandler)->ProcessMessage( TheMessage ) )
		  {
				MessageServed = true;

				// One learn over time which messages that are most frequently used, 
				// and move the forwarding functions for these messages forward in 
				// the handler structure. There are several known strategies known 
				// from the literature. The most famous one is to move the created 
				// message to the front of the list. However, this runs the risk that 
				// if the list is in an organised state, a simple access to one of 
				// the infrequently sent messages will will destroy the organisation 
				// and it will take many messages before the list is again organised. 
				
				// Alternatively, the received message can be moved k elements forward
				// in the list. A compromise between these two is to move the element 
				// to position k+1 if it it is in the end part of the list (move 
				// almost to front), and the transpose the element with the element 
				// in front if it is in the 2..k positions of the list. This 
				// heuristic is known as the POS(k) rule. Two different strategies 
				// are suggested in Oommen et al. (1990): "Deterministic optimal and 
				// expedient move-to-rear list organizing strategies", Theoretical 
				// Computer Science, Vol. 74, No. 2, pp. 183-197. Their first and 
				// optimal strategy implies keeping a counter for each element, and 
				// then move the accessed element to the rear of the list once it has
				// been accessed T times. Their expedient strategy implies moving the 
				// element to the rear of the list when it has been accessed T 
				// consecutive times. The implementation cost of the first rule would
				// be similar to keeping a map sorted on the number of times each 
				// message has arrived - and this map will obviously be exact. Waiting
				// for T consecutive accesses for an element may again be too slow. 
				
				// The transposition rule suggested by Ronald Rivest (1976): "On 
				// self-organizing sequential search heuristics", Communications of 
				// the ACM, Vol. 19, No. 2, pp. 63-67 is much more efficient. Here a 
				// successfully constructed message will be swapped with the element 
				// immediately in front unless the element is already at the start of 
				// the list. It is necessary to use a separate swap iterator to ensure 
				// that the current handler iterator points to the next handler not 
				// affected by the swap operation. 

				auto SuccessfulHandler = CurrentHandler++;
				
				if( SuccessfulHandler != MessageHandlers.begin() )
					std::iter_swap( SuccessfulHandler, std::prev( SuccessfulHandler ) );
			}
			else	
				++CurrentHandler; // necessary because of the transposition rule 
				
			// The Current Handler is now safely set to a handler that is valid for 
			// the next execution, and the handler just executed can be deleted, or
			// its state can be switched back to normal.
			
			if ( (*ExecutingHandler)->GetStatus() == GenericHandler::State::Deleted )
				MessageHandlers.erase( ExecutingHandler );
			else
				(*ExecutingHandler)->SetStatus( GenericHandler::State::Normal );
		}
		
		// If the message is not served at this point, it should be delivered to 
		// the fall back handler. If that handler does not exist it should either 
		// be ignored or an error message will be thrown.
		
		if ( ! MessageServed )
		{
			if ( DefaultHandler )
				DefaultHandler->ProcessMessage( TheMessage );
			else if ( MessageErrorPolicy == MessageError::Throw )
		  {
				std::ostringstream ErrorMessage;
				auto RawMessagePointer = *(Mailbox.front());
				
				ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
										 << "No message handler for the message " 
										 << typeid( RawMessagePointer ).name() 
										 << " and no default message handler!";
										 
			  throw std::logic_error( ErrorMessage.str() );
			}
		}
		
		// The message is fully handled, and it can be popped from the queue and 
		// thereby prepare the queue for processing the next message.
		
		Mailbox.DeleteFirstMessage();
		
		// The callback that a new message has arrived and is processed is given
		// in case derived classes need this information.
		
		MessageProcessed();
		
		// Finally, the thread yields before processing the next message to allow
		// other threads and actors to progress in parallel.
		
		std::this_thread::yield();
		
	}     // While Actor is running
}				// Dispatch function


// When the Postman has processed a message it will notify the actor about this 
// event by calling the virtual message processed function, which will in turn 
// notify other threads that may wait for the condition variable.

void Theron::Actor::MessageProcessed( void )
{ }

// The function to wait for the mailbox to drain is identical to the previous,
// except that it uses a different predicate function to the wait to avoid the 
// additional overhead of repeatedly calling the wait for one message and 
// acquire the lock (although it will happen implicitly in the wait condition)

void Theron::Actor::DrainMailbox( void )
{
	if ( std::this_thread::get_id() != Postman.get_id() )
		Mailbox.WaitUntilEmpty();
	else
	{
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << "Actor " << ActorID.AsString() 
								 << "Cannot wait for message DRAIN within the same actor!";
		
		throw std::logic_error( ErrorMessage.str() );
	}
}

// The identification's function to wait for global termination will start 
// from the beginning of the registry of Identifications and wait for the first
// actor to drain its queue. It will not move to the second actor before the 
// first actor has drained. Whenever an actor is found with messages, the 
// wait will re-start from the beginning of the actor registry because the 
// the actor that had messages could send messages to other actors, and hence 
// it is necessary to wait for them too.

void Theron::Actor::Identification::WaitForGlobalTermination( void )
{
	auto ActorReference = ActorsByID.begin();
	
	while ( ActorReference != ActorsByID.end() )
  {
		Actor * TheActor = ActorReference->second->ActorPointer;
		
		if ( ( TheActor != nullptr ) && ( TheActor->GetNumQueuedMessages() > 0 ) )
		{
			TheActor->DrainMailbox();
			ActorReference = ActorsByID.begin();
		}
		else
			++ActorReference;
	}
}

/*=============================================================================

 Destructor

=============================================================================*/

Theron::Actor::~Actor()
{
	// First the postmaster is told to close. This is done by first waiting for 
	// it to drain the current message queue so that already received messages 
	// can be properly processed.
	
	Mailbox.WaitUntilEmpty();
	
	// Then the flag is set to ensure that the Postman will stop and not process 
	// any messages arriving after this.
	
	ActorRunning = false;
	
	// Other actors should no longer be able to route messages to this actor
	// and the actor pointer in the Identification object should be cleared.
	
	Identification::ClearActor( ActorID );
	
	// To force a stop, an empty message is queued in the case the Postman has 
	// gone into a wait for the next message. This should wake it up and make it
	// check the actor running flag.
	
	Mailbox.StoreMessage( std::make_shared< GenericMessage >() );
	
	// Then it is just to wait for the Postman to finish off
	
	if ( Postman.joinable() )
		Postman.join();	
}
