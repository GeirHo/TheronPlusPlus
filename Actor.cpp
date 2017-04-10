/*=============================================================================
Actor

Author and Copyright: Geir Horn, 2017
License: LGPL 3.0
=============================================================================*/

#include "Actor.hpp"

/*=============================================================================

 Actor identification (alias Framework)

=============================================================================*/

// Static members shared by all actors

std::atomic< Theron::Actor::Identification::IDType >
				Theron::Actor::Identification::TotalActorsCreated;

std::unordered_map< std::string, Theron::Actor::Identification * >
				Theron::Actor::Identification::ActorsByName;
				
std::unordered_map< Theron::Actor::Identification::IDType, 
										Theron::Actor::Identification * >
				Theron::Actor::Identification::ActorsByID;

std::mutex Theron::Actor::Identification::InformationAccess;

Theron::Actor * 
Theron::Actor::RemoteIdentity::ThePresentationLayerServer = nullptr;

// For compatibility reasons there is a global framework pointer

Theron::Framework * Theron::Actor::GlobalFramework = nullptr;

// -----------------------------------------------------------------------------
// Static functions 
// -----------------------------------------------------------------------------
//
// The static function to return the actor pointer based on a an address will
// throw an invalid argument if the given address is Null

Theron::Actor * Theron::Actor::Identification::GetActor( 
																	 const Theron::Actor::Address & ActorAddress )
{
	if ( ActorAddress )
  {
		Actor * TheActor = ActorAddress.TheActor->ActorPointer;
		if ( TheActor != nullptr )
			return TheActor;
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
																									const std::string ActorName )
{
	std::lock_guard< std::mutex > Lock( InformationAccess );
	
	auto TheActor = ActorsByName.find( ActorName );
	
	if ( TheActor == ActorsByName.end() )
		return Theron::Actor::Address();
	else
		return Theron::Actor::Address( TheActor->second );
}

// The second lookup function is almost identical except that it uses the ID 
// map to find the actor.

Theron::Actor::Address Theron::Actor::Identification::Lookup(
																														const IDType TheID )
{
	std::lock_guard< std::mutex > Lock( InformationAccess );
	
	auto TheActor = ActorsByID.find( TheID );
	
	if ( TheActor == ActorsByID.end() )
		return Theron::Actor::Address();
	else
		return Theron::Actor::Address( TheActor->second );
}

// The function setting the presentation layer will throw a logic error if 
// another actor already has claimed the role of a presentation layer, and the 
// given pointer is not null, i.e. that this is invoked by an actor currently 
// acting as the presentation layer, but about to close.

void Theron::Actor::RemoteIdentity::SetPresentationLayerServer( 
																											Theron::Actor * TheSever )
{
  if ( TheSever == nullptr )
	{
		// The session layer is de-registering and all external references to this 
		// session layer server should be removed - with no session layer server
		// it is not possible to communicate externally and the external references
		// should be invalidated.
		// TODO: Implement this functionality
	}
	else if ( ThePresentationLayerServer == nullptr  )
		ThePresentationLayerServer = TheSever;
	else 
	{
		std::ostringstream ErrorMessage;
		 
		ErrorMessage << "The presentation layer server is already set to actor "
								 << ThePresentationLayerServer->ActorID.Name;
								 
	  throw std::logic_error( ErrorMessage.str() );
	}
		
}

// -----------------------------------------------------------------------------
// virtual functions 
// -----------------------------------------------------------------------------

void Theron::Actor::EndpointIdentity::Register( Address * NewAddress )
{
	std::lock_guard< std::recursive_mutex > Lock( AddressAccess );
	Addresses.insert( NewAddress );
}

void Theron::Actor::EndpointIdentity::DeRegister( Address * OldAddress )
{
	std::lock_guard< std::recursive_mutex > Lock( AddressAccess );
	Addresses.erase( OldAddress );
}

void Theron::Actor::RemoteIdentity::Register( Address * NewAddress )
{
	NumberOfAddresses++;
}

// The remote identity is created whenever an actor refers to an address by 
// name only and this address cannot be found on the current endpoint. It will 
// count how many addresses that refer to this remote actor, and if there are 
// no more addresses then the entry can be removed. 
//
// Note that this strategy may allow other actors to be constructed locally with
// the same name as the remote actor was known to have, and local actors will 
// prevent the creation of references to remote actors with the same name.

void Theron::Actor::RemoteIdentity::DeRegister( Address * OldAddress )
{
	if ( --NumberOfAddresses == 0 )
		delete this;
}

// The registration and de-registration functions for the Deleted Identity 
// are identical to the ones for the remote identity.

void Theron::Actor::DeletedIdentity::Register( Address* NewAddress )
{
	NumberOfAddresses++;
}

void Theron::Actor::DeletedIdentity::DeRegister( Address * OldAddress )
{
	if ( --NumberOfAddresses == 0 )
		delete this;
}


// -----------------------------------------------------------------------------
// Constructors and Destructors 
// -----------------------------------------------------------------------------
//

// The "copy" constructor of the Identification is provided for another identity
// to mask as an existing identity. The issue is with the automatically assigned
// ID since addresses are sorted in containers based on their actor's numerical
// IDs, and then one can cannot remove an ID without that have active addresses
// referring to this Identification. The solution is to create a deleted 
// identity and make this assume the given actor's name and numerical ID, but 
// not the actor pointer. This is only an issue for endpoint local actors and 
// it is therefore required that the given pointer is to an endpoint identity.

Theron::Actor::Identification::Identification( 
																					const EndpointIdentity * TheActorID )
: NumericalID( TheActorID->NumericalID ), Name( TheActorID->Name ),
  ActorPointer( nullptr )
{ }


// The constructor of the identification simply stores the name in 

Theron::Actor::EndpointIdentity::EndpointIdentity( 
		Theron::Actor * TheActor, const std::string & ActorName )
	: Identification( TheActor, ActorName ),
	  Addresses(), AddressAccess()
{
	std::lock_guard< std::mutex > Lock( InformationAccess );
	
	auto Outcome = ActorsByName.emplace( Name, &(TheActor->ActorID) );
	
	if ( Outcome.second != true )
	{
		std::ostringstream ErrorMessage;
		
		ErrorMessage << "An actor with the name " << Name 
								 << " does already exist! This ID = " << NumericalID
								 << " other ID = " << Outcome.first->second->NumericalID;
		
		throw std::invalid_argument( ErrorMessage.str() );
	}
	
	ActorsByID.emplace( NumericalID, &(TheActor->ActorID) );
}

// In the optimal situation there are no more addresses referencing the 
// actor having this Endpoint Identity, and then it can just be taken out of 
// the actor registries. However, if there are addresses still referencing 
// the actor, a Deleted Identification object must be constructed and all the 
// referencing actors must be set to the Deleted Address, which will be 
// responsible for cleaning up the registrations when it eventually terminates.

Theron::Actor::EndpointIdentity::~EndpointIdentity()
{
	// First the lock for accessing the name and ID registries is obtained since
	// it is needed regardless of whether there are addresses referencing this 
	// actor or not.
	
	std::lock_guard< std::mutex > InformationLock( InformationAccess );

	// Then the further actions depend on the number of address objects known
	
	if ( Addresses.empty() )
  {
		// The case is simple: The endpoint identity can just be removed and this
		// node forgets about the actor entirely.
		
		ActorsByName.erase( Name 				);
		ActorsByID.erase  ( NumericalID );
	}
	else
  {
		// In this case a deleted identity must be created to allow addresses to 
		// remain local information sources, but disallow further messages to be 
		// sent to this destroyed actor. This ghost will destroy itself once the 
		// last address de-register, and so there should be no risk of a memory 
		// leak.
		
		DeletedIdentity * TheActorGhost = new DeletedIdentity( this );
		
		// This ghost will replace this actor in the registries.
		
		ActorsByName[ Name 				] = TheActorGhost;
		ActorsByID  [ NumericalID ] = TheActorGhost;
		
		// Then the referencing addresses can be "re-constructed" with the ghost 
		// as the identifier for the actor. The address lock must be acquired first
		
		std::lock_guard< std::recursive_mutex > AddressLock( AddressAccess ); 
		
		for ( Address * TheAddress : Addresses )
		{
			TheAddress->TheActor = TheActorGhost;
			TheActorGhost->Register( TheAddress );
		}
		
		// Eventually the references back to the the addresses are cleared.
		
		Addresses.clear();
	}
}

// The constructor for the remote identity stores the identity in the registry 
// for actors by name if the Session Layer server is set. Otherwise it will 
// throw a logic error since remote addresses cannot be used without a session
// layer server. 
// 
// It is similar to the end point identity in that it will check that there is 
// no actor by this name already, even though this test should not be necessary
// it is included as an additional precaution. However, it will not store 
// the ID because the numerical ID is valid only on this endpoint, and it 
// will check the availability of the Session Server as a pre-requisite.

Theron::Actor::RemoteIdentity::RemoteIdentity( const std::string & ActorName )
: Identification( ThePresentationLayerServer, ActorName ),
  NumberOfAddresses(0)
{

	if ( ThePresentationLayerServer != nullptr )
  {
		std::lock_guard< std::mutex > Lock( InformationAccess );
		
		auto Outcome = ActorsByName.emplace( Name, this );
		
		if ( Outcome.second != true )
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << "An actor with the name " << Name 
									 << " does already exist!";
			
			throw std::invalid_argument( ErrorMessage.str() );
		}
		else
			ActorsByID.emplace( NumericalID, this );
	}
	else
		throw std::logic_error("Remote actor IDs requires a Session Layer Server");
	
}

// When the remote identity is no longer referenced it should remove its ID 
// and name from the registries.

Theron::Actor::RemoteIdentity::~RemoteIdentity( void )
{
	std::lock_guard< std::mutex > Lock( InformationAccess );
	
	ActorsByName.erase( Name );
	ActorsByID.erase  ( NumericalID );
}

// Since the Identification guarantees that each ID is unique, it will assign 
// a new ID for the Deleted Identity. Hence, the deleted identity needs to 
// look after two IDs: The one of the original actor and the ID automatically
// assigned. Note that only the latter needs to be registered as the one 
// that was assigned to the deleted actor is transferred by the destructor of 
// the Endpoint Identity.

Theron::Actor::DeletedIdentity::DeletedIdentity( 
																				  const EndpointIdentity * IDToRemove )
: Identification( IDToRemove ), 
  NumberOfAddresses(0)
{ }

// Similar to the remote identity the destructor has to clean up the 
// registrations.

Theron::Actor::DeletedIdentity::~DeletedIdentity( void )
{
	std::lock_guard< std::mutex > Lock( InformationAccess );
	
	ActorsByName.erase( Name );
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
		EndpointIdentity * TheID = 
									dynamic_cast< EndpointIdentity * >( ActorReference->second );
		
		// If the actor has messages, the mailbox should be drained. After that, 
		// the iteration will restart from the beginning of the actor registry 
		// because actors previously without messages may now have messages. 
		// Otherwise, the search just continues with the next actor.
									
		if ( TheID && (TheID->ActorPointer->GetNumQueuedMessages() > 0) )
		{
			TheID->ActorPointer->DrainMailbox();
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
