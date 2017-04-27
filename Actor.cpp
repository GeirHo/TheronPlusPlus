/*=============================================================================
Actor

Author and Copyright: Geir Horn, 2017
License: LGPL 3.0
=============================================================================*/

#include "Actor.hpp"
#include "PresentationLayer.hpp"

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
		throw std::logic_error( "Only one presentation server can be used" );
	
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
	// new Identification is again constructed. Note that the constructor takes
	// care of inserting the Identification object in the two lookup maps.
	
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
	
	// If the actor pointer is given, this should replace the actor pointer of 
	// the Identification object provided that this pointer is not set. If the 
	// pointer has a value there is a serious logical error in the application 
	// and an exception should be thrown.
	
	if ( TheActor != nullptr )
  {
		if ( TheActorID->ActorPointer == nullptr )
			TheActorID->ActorPointer = TheActor;
		else if( TheActor != TheActorID->ActorPointer )
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << "Attempt to Create an Identification with name "
									 << ActorName << " and address " << TheActor 
									 << " but this named Identification already points "
									 << " to the actor at " << TheActorID->ActorPointer;
									 
		  throw std::logic_error( ErrorMessage.str() );
		}
	}
	
	// At this point the Identification pointer is correctly set, and the 
	// address constructed from its shared pointer can be returned.
	
	return Address( TheActorID );
}

// The static function to return the actor pointer based on a an address will
// throw an invalid argument if the given address is Null

Theron::Actor * Theron::Actor::Identification::GetActor( 
																	 const Theron::Actor::Address & ActorAddress )
{
	if ( ActorAddress )
  {
		Actor * TheActor = ActorAddress->ActorPointer;
		if ( TheActor != nullptr )
			return TheActor;
		else if ( ThePresentationLayerServer )
			return ThePresentationLayerServer->ActorPointer;
		else
			throw std::invalid_argument( "GetActor: NULL actor pointer");
  }
	else
		throw std::invalid_argument( "GetActor: NULL actor address" );
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

// When an actor sends a message to another actor, it will call the 
// enqueue message function on the receiving agent with a pointer to a copy of
// the message to ensure that it does exists also when the message is handed 
// by the receiving actor.
//
// The enqueue function will first append the message to the message queue, and 
// if no postman in running, it will start the thread to dispatch the message 
// to the right handler.

bool Theron::Actor::EnqueueMessage( 
													const	std::shared_ptr< GenericMessage > & TheMessage )
{
	// Enqueue the message 
	
	std::unique_lock< std::timed_mutex > QueueLock( QueueGuard, 
																									std::chrono::seconds(5) );
	Mailbox.push( TheMessage );
	QueueLock.unlock();
	
	// The postman should be signalled that processing should continue if it is 
	// waiting for messages, i.e. if the queued message was the first message 
	// in the queue. Otherwise, the Postman is already processing messages and 
	// will come to this queued message at one point.
	
	std::unique_lock< std::timed_mutex > ProcessLock( Postoffice, 
																						        std::chrono::seconds(5) );
	NewMessageAvailable = true;
	ContinueMessageProcessing.notify_one();
	ProcessLock.unlock();
	
	return true;
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
	// The first thing to do is to tell the Actor constructor that the postman 
	// thread is up running and ready to dispatch messages.
	
	{
		std::lock_guard< std::timed_mutex > Guard( Postoffice );
		ContinueMessageProcessing.notify_one();
	}
	
	// Then the processing loop can be started and this will run as long as 
	// the actor is running.
	
	while ( ActorRunning )
  {
		// The thread should just wait until messages becomes available.
		
		if ( Mailbox.empty() )
		{
			std::unique_lock< std::timed_mutex > Lock( Postoffice, 
																								 std::chrono::seconds(5) );
			ContinueMessageProcessing.wait( Lock, 
				[&](void)->bool{ return NewMessageAvailable || (!ActorRunning); });
			
			NewMessageAvailable = false;
		}
		else
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
					
					// Then the list of handlers is optimised by swapping the current 
					// handler with the handler in front unless it is already the first 
					// handler. It is necessary to use a separate swap iterator to ensure 
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
					
					ErrorMessage << "No message handler for the message " 
											 << typeid( RawMessagePointer ).name() 
											 << " and no default message handler!";
											 
				  throw std::logic_error( ErrorMessage.str() );
				}
			}
			
			// The message is fully handled, and it can be popped from the queue and 
			// thereby prepare the queue for processing the next message.
			
			std::unique_lock< std::timed_mutex > QueueLock( QueueGuard, 
																											std::chrono::seconds(5) );
			Mailbox.pop();
			QueueLock.unlock();
			
			// The callback that a new message has arrived and is processed is given
			// in case derived classes need this information.
			
			MessageArrived();
			
			// Finally, the thread yields before processing the next message to allow
			// other threads and actors to progress in parallel.
			
			std::this_thread::yield();
			
		}   // Queue was not empty
	}     // While Actor is running
}				// Dispatch function

// The wait function will mirror the waiting behaviour of the message dispatcher
// function in that it will lock the mutex and then set the flags. It will also
// count the number of messages processed and return when the requested number 
// has been received.

/*
Theron::Actor::MessageCount Theron::Actor::Wait( 
																							const MessageCount MessageLimit )
{
	// Counting the received messages
	
	MessageCount Counter = 0;

	// Then acquiring the lock to be released when the conditional wait starts,
	// or when the lock object is destroyed. 
	
	std::unique_lock< std::mutex > MessageLock( WaitGuard );
		
	// There is now one more Wait function in effect.
	
	WaiterCount++;
	
	// Message are accounted for and acknowledged one by one until the message 
	// limit is reached
	
	while ( ActorRunning && (Counter < MessageLimit) )
  {
		// The New Message flag is cleared as it will be set before all the waiting
		// threads are notified and is used to check that the return from the wait
		// is for real.
		
		NewMessage = false;

		// It is just to wait for the next message to arrive. This will release the 
		// lock while waiting.
		
		OneMessageArrived.wait(MessageLock, [&](void)->bool{ return NewMessage; });
		
		// After this thread has been notified by the Postman about the new message
		// available, the lock is again set and it is just to increase the message
		// count.

		Counter++;
	}
	
	// Then this Wait function is done with its wait and can de-register
	
	WaiterCount--;
	
	// For the sake of compatibility the number of messages handled will be 
	// returned to the calling thread. If the abort wait is set, then the it was
	// counted as a message and it should be subtracted from the count of real 
	// messages before the value is returned.
	
	if ( ActorRunning )
		return Counter;
	else
		return --Counter;
}
*/
// The wait for global termination is based on the fact that only internal 
// actors register by ID, so it is safe to go through the registry and join 
// threads that are working until all queues are empty and all threads have 
// stopped. 

void Theron::Actor::Identification::WaitForGlobalTermination( void )
{
	auto ActorReference = ActorsByID.begin();
	
	while ( ActorReference != ActorsByID.end() )
  {
		Actor * TheActor = ActorReference->second->ActorPointer;
		
		// If the actor has messages, the mailbox should be drained. After that, 
		// the iteration will restart from the beginning of the actor registry 
		// because actors previously without messages may now have messages. 
		// Otherwise, the search just continues with the next actor.
									
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
	// First the postmaster is told to close if it is pending more messages. One
	// may always argue that the actor running flag should be atomic or protected
	// by a mutex, however there is only one worker and it should be stalled at 
	// this point because the message queue should be empty. Hence it will not 
	// do anything before it gets notified and then it realises that it is time 
	// to stop.
	
	ActorRunning = false;
	
	ContinueMessageProcessing.notify_one();
	
	// This should allow the Postman to empty the message queue, and so it is 
	// just to wait for this to happen if the Postman is still working.
	
	if ( Postman.joinable() )
		Postman.join();	
}
