/*==============================================================================
Scheduled

A code file is needed because of the static variables in use. Thus the file may
equally well contain the implementations of the non-template functions. 

The Gnu Multi Precision (GMP) library is used and one should therefore link 
code built with scheduled objects with the necessary libraries

-lgmpxx -lgmp

Author and Copyright: Geir Horn, 2018
License: LGPL 3.0
==============================================================================*/

#include "Scheduled.hpp"

/*=============================================================================

 Static variables

=============================================================================*/

std::shared_ptr< Theron::Scheduled > Theron::Scheduled::Object::TheScheduler;

std::mutex Theron::Scheduled::Object::RegistryLock;
std::unordered_map< Theron::Scheduled::ObjectIdentifier, 
										Theron::Scheduled::Object * > 
										Theron::Scheduled::Object::TheRegistry;

/*=============================================================================

 Scheduled object

=============================================================================*/
//
// The constructor takes the name of the executable object, and if no name 
// is given it will be constructed. Then it tries to register this in the 
// registry. If the name already exists, an logic error exception is thrown 
// with the information that the object name already exists.
//
// If no name was given, the function constructing the name also inserts 
// this name in the registry, and it is just a matter of updating the 
// pointer to this. If the name was given it must be registered in the 
// registry and checked for validity.
// 
// It should be noted that the static function to create a scheduler should 
// be used before creating the objects, otherwise the object will create a 
// default scheduler if there is no scheduler existing.

Theron::Scheduled::Object::Object( std::string Name )
: ID( Name.empty() ? MakeID() : Name ), 
  PendingMessages(0), QueueEmpty()
{
	// Then the scheduler is created if it does not already exists
	
	if ( !TheScheduler )
		MakeScheduler< Scheduled >();

	// Then this object can be registered in the object registry
	
	std::lock_guard< std::mutex > Lock( RegistryLock );
	
	if ( Name.empty() )
		TheRegistry[ ID ] = this;
	else
  {
		auto Result = TheRegistry.emplace( Name, this );
		
		if ( Result.second == false )
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
			             << "Error constructing scheduled object: " << Name 
									 << " is already used by another object";
									 
		  throw std::logic_error( ErrorMessage.str() );
		}
	}			
}

//
// If there are messages pending processing when the destructor is called,
// processing should continue as normal until there are no more messages 
// and then the ID of this object will be removed so no further messages 
// can be sent to this object. It will inform the scheduler when the 
// thread blocks and unblock. There is a small problem that the queue can 
// drain, but a new message created before the address has been removed.
//

Theron::Scheduled::Object::~Object( void )
{
	std::unique_lock< std::mutex > QueueLock( RegistryLock );
	
	// The queue is drained if there are pending messages. If there are no 
	// messages or if the queue is drained in one go, the loop is equivalent 
	// to an if test. It will first tell the scheduler that this worker 
	// thread is blocking, and when the queue is empty, it will inform the  
	// scheduler that the thread is active again. 
	
	while ( PendingMessages.load() > 0 )
  {
		TheScheduler->Send( WorkerBlocked( std::this_thread::get_id() ), 
												Address(), TheScheduler->GetAddress() ); 
		
		QueueEmpty.wait( QueueLock, [&](void)->bool{ 
			return PendingMessages.load() == 0; } );
		
		TheScheduler->Send( WorkerRunning( std::this_thread::get_id() ), 
												Address(), TheScheduler->GetAddress() ); 				
	}
	
	// Finally the reference to this object is removed so that no further 
	// messages can be generated for this object. This is safe as the 
	// condition variable locks the registry when the wait function returns.
	
	TheRegistry.erase( ID );	
}

/*=============================================================================

 Queue length control

=============================================================================*/
//
// The handler recording object message arrival times is slightly  
// complicated since it also computes the inter-arrival time and adds this 
// to the global counters.

void Theron::Scheduled::QueueLengthControl::RecordArrivalTime(
	const Theron::Scheduled::QueueLengthControl::ObjectMessageArrival & TheSignal, 
  Theron::Actor::Address Sender )
{
	// The inter arrival time is computed and this arrival time is recorded 
	// for computing the next inter arrival time.
	
	std::chrono::system_clock::duration InterArrival =
			TheSignal.TimeStamp - LastArrival;
	
	LastArrival = TheSignal.TimeStamp;
			
	// Update the counters for the arrival
			
	TotalInterArrivalTime += InterArrival.count();
	NumberOfMessages++;
	MessagesInTheSystem++;
	
	// Compute the average inter arrival time for being able to calculate
	// expected queue length when the message has been serviced.
	
	Lambda = mpq_class( TotalInterArrivalTime, NumberOfMessages );
	
	// The counters involved in the last line will increase for each message,
	// which is why they are multi-precision numbers, but they will consume 
	// more memory the longer they become, and one may expect that
	// computations like the one above for Lambda will be more complex and 
	// take longer the longer the involved numbers become. How to decide 
	// when the counters can be reset?
	//
	// The idea is to look at how the counters are used, i.e. to compute 
  // Lambda which is in turn converted to a double in the expressions for
	// the expected queue length. Thus if the conversion to double gives the 
	// same double number even if the counters are updated because the double
	// does not have sufficient precision to capture the update. 
	// 
	// However, it is not necessary to make this assessment every update, 
	// and it is done only under the same conditions as the number of workers
	// is assessed when the message process time is recorded below. 
	
	if ( ( MessagesInTheSystem < NumberOfWorkers ) || 
		   ( MessagesInTheSystem - NumberOfWorkers > Watermark ) )
	{
		mpq_class PastLambda( TotalInterArrivalTime - InterArrival.count(), 
													NumberOfMessages - 1 );
		
		// Comparing real numbers is always difficult because they are 
		// imprecise owing to the limited precision of the mantissa. C++11 
		// provided a function to test if two real numbers are unequal.
		
		if ( ! std::islessgreater( Lambda.get_d(), PastLambda.get_d() ))
		{
			TotalInterArrivalTime = Lambda;
			NumberOfMessages      = 1;
		}
	}
}

//
// The signal that message for an executable object has been allocated to a 
// worker and thus its processing started stores the entry in the service 
// start time map. 
//

void Theron::Scheduled::QueueLengthControl::RecordStartTime( 
	const Theron::Scheduled::QueueLengthControl::ProcessingStart & TheSignal,
	Theron::Actor::Address Sender )
{
	ServiceStartTimes.emplace( TheSignal.WorkerAddress, TheSignal.TimeStamp );
}

//
// Ending the processing of an object message will first compute the 
// service time duration and update the counters and remove the remembered
// start time, similar to the arrival time handler. However, it will then 
// re-assess the number of workers by computing the expected queue size
// for the current number of workers. If this is larger than the watermark,
// an additional worker is requested. If it is less than the threshold, 
// the expected queue size is computed for one worker less. If this is 
// still less than the watermark threshold, the scheduler is requested to 
// remove one worker.
//

void Theron::Scheduled::QueueLengthControl::RecordProcessingTime(
	const Theron::Scheduled::QueueLengthControl::ProcessingStop & TheSignal, 
	Theron::Actor::Address Sender)
{
	// The messages must be acknowledged in sequence, so the smallest start 
	// time if found in the range of start times set for this worker. The 
	// found time is taken as consumed and removed from the set of start 
	// times.
	
	auto AvailableStartTimes = 
			 ServiceStartTimes.equal_range( TheSignal.WorkerAddress );
			 
  auto ServiceStart = std::min_element( AvailableStartTimes.first, 
																				--AvailableStartTimes.second,
	     []( const std::pair< Address, 
								 std::chrono::system_clock::time_point > & Candidate, 
				   const std::pair< Address, 
								 std::chrono::system_clock::time_point > & Smallest
			   )->bool{ return Candidate.second < Smallest.second; }	);
			 
	std::chrono::system_clock::duration ServiceTime = 
		TheSignal.TimeStamp - ServiceStart->second;

	ServiceStartTimes.erase( ServiceStart );

	// Then the counters are updated 
		
	TotalServiceTime += ServiceTime.count();
	MessagesServiced++;
	
	MessagesInTheSystem--;
	
	// The real queue length is compared with the watermark threshold. If it 
	// is larger than the threshold, it could be a temporary problem, or it 
	// could be that more workers should be added. 
	
	if ( ( MessagesInTheSystem < NumberOfWorkers ) || 
		   ( MessagesInTheSystem - NumberOfWorkers > Watermark ) )
	{
		// Compute the average inter-arrival and service time. Both of these 
		// will be clock ticks and should fit into the representation of the
		// system clock's duration object.
		
		mpq_class Mu( TotalServiceTime,  MessagesServiced );
						  
		// The average utilisation rate and the utilisation rate can basically
		// only be used as doubles and they are therefore computed as 
		// rationales and converted to doubles.
		
		double AvgRho =  mpq_class( Lambda / Mu ).get_d() ,
					 Rho    =  AvgRho / static_cast< double >( NumberOfWorkers );
							
		// The first step is to compute the denominator sum of Erlang's C 
		// formula for one worker less than the current number of workers, in
	  // case this is needed to scale in the number of workers.
							
		double ScaleInSum = 0.0;
		
		for ( WorkerCounterType k = 0; k < NumberOfWorkers-2; k++ )
			ScaleInSum += std::pow( AvgRho, k ) / WorkerFactorials[ k ].get_d() ;
	
	  // The denominator of Erlang's C formula is then constructed stepwise 
	  // from the end starting with the sum involved in the sum for the 
		// number of workers is computed by adding the last term for k-1
															
		double Denominator = 
					 ScaleInSum + std::pow( AvgRho, NumberOfWorkers - 1 )
											  / WorkerFactorials[ NumberOfWorkers - 1 ].get_d();
		
		// Then this sum is scaled 
												
		Denominator *= WorkerFactorials[ NumberOfWorkers ].get_d() / 
									 std::pow( AvgRho, NumberOfWorkers );
		
	  // Then this is multiplied with (1-rho)
		
		Denominator *= 1.0 - Rho;
											 
		// Finally, unity can be added to form the complete expression
		
		Denominator += 1.0;

    // The probability for a message to be queued and not served is then 
		// computed as the reciprocal of this, multiplied with the 
		
		std::queue< ObjectMessage >::size_type ExpectedQueueLength =
		std::lround( ( Rho/( 1.0 - Rho ) ) * (1.0 / Denominator ) + AvgRho );
		
		// The expected queue size is compared with the watermark threshold 
		// to decide if workers should be added or removed. However, it will 
		// always keep as many workers as there are hardware threads.
							
		if ( ExpectedQueueLength > Watermark )
		{
			Send( CreateWorker(), Sender );
			NumberOfWorkers++;
			
			if ( WorkerFactorials.size() < NumberOfWorkers )
				WorkerFactorials.push_back( 
												 WorkerFactorials.back() * NumberOfWorkers );
		}
		else if ( NumberOfWorkers > std::thread::hardware_concurrency() )
		{
			// Starting from the scale in sum, the expected queue length is 
			// again computed by the scaling of the denominator, and multiplying
			// with (1-rho) and adding unity.
			
			Denominator = ScaleInSum;
			Denominator *= WorkerFactorials[ NumberOfWorkers - 1 ].get_d() / 
										 std::pow( AvgRho, NumberOfWorkers - 1 );
		  Denominator *= 1.0 - Rho;
			Denominator += 1.0;
			
			// The the expected queue length is again computed as before
			
			ExpectedQueueLength = std::lround( 
				( Rho	/ (1.0 - Rho) ) * (1.0 / Denominator ) + AvgRho );
			
			// If this is less than the watermark, then a worker can be removed
			
			if ( ExpectedQueueLength < Watermark )
			{
				Send( RemoveWorker(), Sender );
				NumberOfWorkers--;
			}
		}
		
		// After the assessment of the numbers of workers, an evaluation is 
		// done to see if the counters for the service time should be 
		// renormalised. See the inter arrival time computation above for 
		// discussion and details.
		
		mpq_class PastMu( TotalServiceTime - ServiceTime.count() , 
											MessagesServiced - 1 );
		
		if ( ! std::islessgreater( Mu.get_d(), PastMu.get_d() ) )
		{
			TotalServiceTime = Mu;
			MessagesServiced = 1;
		}
	}
}

//
// The constructor takes the watermark value and the address of the 
// scheduler actor and asks it to create the number of workers supported by 
// the current hardware 
//

Theron::Scheduled::QueueLengthControl::QueueLengthControl( 
	const Theron::Actor::Address& SchedulerActor, 
	std::queue< ObjectMessage >::size_type QueueLengthThreshold, 
	std::string Name )
: Actor( Name ), NumberOfWorkers(0), 
  Watermark( QueueLengthThreshold ), MessagesInTheSystem(0),
  TotalInterArrivalTime(0), NumberOfMessages(0),
  LastArrival( std::chrono::system_clock::now() ), Lambda(),
  TotalServiceTime(0), MessagesServiced(0),
  ServiceStartTimes(), WorkerFactorials()
{
	// First the message handlers are registered to be ready to receive 
	// messages immediately.
	
	RegisterHandler( this, &QueueLengthControl::RecordArrivalTime    );
	RegisterHandler( this, &QueueLengthControl::RecordStartTime      );
	RegisterHandler( this, &QueueLengthControl::RecordProcessingTime );
	
	// Then decide the number of initial workers.
	
	WorkerFactorials.push_back( 1 );
	
	for ( unsigned int k = 1; k <= std::thread::hardware_concurrency(); k++ )
  {
		NumberOfWorkers++;
		WorkerFactorials.push_back( WorkerFactorials.back() * NumberOfWorkers );
	}
	
	// Finally the scheduler is requested to start the workers. This second
	// loop is necessary to ensure that the worker factorials had been 
	// created before the workers are started if the scheduler starts quickly
	// to dispatch messages.
	
	for ( unsigned int k = 1; k <= NumberOfWorkers; k++ )
		Send( CreateWorker(), SchedulerActor );
}

/*=============================================================================

 Worker

=============================================================================*/
//
// The main handler is the one that respond to message wrapper from an object
// and will try to deliver this to the handlers in sequence until all 
// handlers have been tried. All handlers must be tried since multiple 
// handlers can be registered for a message type.

void Theron::Scheduled::Worker::ProcessMessage(
	const Theron::Scheduled::ObjectMessage & TheMessage, 
	Theron::Actor::Address TheDispatcher)
{
	// The receiver of the message is looked up in the object registry. There is 
	// no need to lock the access because the object's own termination mechanism
	// ensures that the object always exists when a message should be processed.
	
	auto Result = Object::TheRegistry.find( TheMessage->To );
	
	// If the receiver was found, the message can be delivered in turn to each 
	// of its handlers while checking that at least one of them processed the 
	// messaged.
	
	if ( Result != Object::TheRegistry.end() )
  {
		bool MessageHandled = false;
		
		for ( auto & Handler : Result->second->MesssageHandlers )
		  MessageHandled |= Handler->ProccessMessage( TheMessage );
		
		if ( MessageHandled )
		{
			Send( MessageDone( TheMessage->To ), TheDispatcher );
			(*Result->second->MesssageHandlers.begin())->MessageDone();
		}
		else
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
			             << "Object " << TheMessage->To 
			             << " has no handler for a message received from object "
									 << TheMessage->From;
									 
		  throw std::logic_error( ErrorMessage.str() );
		}
	}
	else
  {
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
		             << "Worker " << GetAddress().AsString() 
								 << " asked to process message to non-existing object "
								 << TheMessage->To << " sent from object " 
								 << TheMessage->From;
								 
	  throw std::logic_error( ErrorMessage.str() );
 	}
}

//
// There is a simple utility handler to respond the thread ID of this worker
// when the Scheduler asks after starting the worker.
//

void Theron::Scheduled::Worker::ReportThread(
	const Theron::Scheduled::GetThreadID & TheRequest, 
	Theron::Actor::Address TheDispatcher)
{
	Send( WorkerRunning( std::this_thread::get_id() ), TheDispatcher );
}

//
// The constructor simply requires an actor ID name which is assigned by the 
// scheduled object, and register the two message handlers.
//

Theron::Scheduled::Worker::Worker( const std::string & Name )
: Actor( Name )
{
	RegisterHandler( this, &Worker::ProcessMessage );
	RegisterHandler( this, &Worker::ReportThread   );
}

/*=============================================================================

 Scheduler

=============================================================================*/
//
// -----------------------------------------------------------------------------
// Managing workers
// -----------------------------------------------------------------------------
//
// The queue length control will send messages to create or delete workers. 
// Creating a new worker is just to insert a new worker into the list of 
// blocked workers, and ask this newly created worker to register its thread.

void Theron::Scheduled::NewWorker(
	const QueueLengthControl::CreateWorker & Command, 
	Theron::Actor::Address Sender )
{
	// The new worker is created in the set of blocked workers since it is not
	// yet active. 
	
  WorkerReference NewWorker = std::make_shared< Worker >();
	
	BlockedWorkers.insert( NewWorker );
	
	// Then the worker is asked to identify its thread and become an active 
	// worker.
	
	Send( GetThreadID(), NewWorker->GetAddress() );
	
	// Finally, the mapping between the address of the worker and its reference
	// is established
	
	AddressView.emplace( NewWorker->GetAddress(), NewWorker );
}

//
// Deleting a worker is more difficult since only passive workers can be 
// removed. If there are no workers in the ready list when a worker should 
// be removed, a counter is incremented to indicate the number of workers
// to remove. Its type is defined in terms of the counter for any of the 
// worker lists.
//
// The message handler can therefore be invoked in a response from the worker
// manager or, in the interest of not duplicating code, called by the message
// handler for completed object messages, i.e. when a worker has finished its 
// current work and could be removed. 

void Theron::Scheduled::DeleteWorker(
	const QueueLengthControl::RemoveWorker & Command, 
	Theron::Actor::Address Sender )
{
	
	if ( Sender == WorkerManager.GetAddress() )
		WorkersToRemove++;
	
	// If there are workers in the ready list, these can safely be removed 
	// as long as there are ready workers.
	
	for ( ; (!IdleWorkers.empty() && ( WorkersToRemove > 0) ); 
			    WorkersToRemove-- )
  {
		WorkerReference WorkerToRemove = *IdleWorkers.begin();
		
		// The first worker in the ready list will be removed. The first 
		// task is to Find and remove this worker from the thread view
				
		auto TReference = std::find_if( ThreadView.begin(), ThreadView.end(),
			[&]( const std::pair< std::thread::id, WorkerReference > & Record )
			->bool{ return Record.second == WorkerToRemove; });
			
		if ( TReference != ThreadView.end() )
			ThreadView.erase( TReference );
		
		// Then find and remove the same worker from the object view
		
		auto OReference = std::find_if( ObjectView.begin(), ObjectView.end(),
			[&]( const std::pair< ObjectIdentifier, WorkerReference > & Record )
			->bool{ return Record.second == WorkerToRemove; });
		
		if ( OReference != ObjectView.end() )
			ObjectView.erase( OReference );
		
		// Removing it from the address view is simpler
		
		auto AReference = AddressView.find( WorkerToRemove->GetAddress() );
		
		if ( AReference != AddressView.end() )
			AddressView.erase( AReference );
		
		// Finally remove the worker entirely
		
		IdleWorkers.erase( WorkerToRemove );
	}
}

// -----------------------------------------------------------------------------
// Worker state
// -----------------------------------------------------------------------------
//
// The change in the execution state is depending on the worker status 
// messages. These can come from the workers or from objects that need to 
// block their worker while waiting for their message queue to drain. The 
// fist handler receives the information that the worker is running, and the 
// state can only be detected from the thread ID as it can either be a starting
// worker registering for the first time, or a previously suspended worker 
// starting up again.
//

void Theron::Scheduled::WorkerActive(
	const Theron::Scheduled::WorkerRunning & Status, 
	Theron::Actor::Address Sender)
{
	auto FoundWorker = ThreadView.find( Status.ThreadID );
	
	// If there was a registration for this thread, the associated worker changes
	// state from blocked to active, and the object is moved between the two 
	// lists. 
	
	if ( FoundWorker != ThreadView.end() )
		ActiveWorkers.insert( BlockedWorkers.extract( FoundWorker->second ) );
	else if ( Sender != Address() )
	{
		// The message is from a new worker whose Thread ID should be registered
		// and we need to find the worker in the blocked list based on its sender
		// address.

		auto TheNewWorker = AddressView.find( Sender );
			
		if ( TheNewWorker != AddressView.end() )
	  {
			// The worker should then first be registered in the thread view
				
			ThreadView.emplace( Status.ThreadID, TheNewWorker->second );
			
			// Then it can be moved to the list of idle workers
			
			IdleWorkers.insert( BlockedWorkers.extract( TheNewWorker->second ) );
			
			// If there are messages queued during the handshake they can now 
			// be dispatched as there is at least one ready worker.
			
			DispatchMessages();
		}
		else
	  {
			// This means that the sender address is not a worker, or the worker 
			// has not been registered in the address view. None of these cases 
			// should ever happen, and if this invariant is violated it reflects 
			// a serious logic error and there is no way to recover.
			
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
			             << "Worker running message from " << Sender.AsString()
									 << " which is not a worker registered in the address"
									 << " view";
									 
		  throw std::logic_error( ErrorMessage.str() );
		}
	}
	else
	{
		// It should never happen that the thread is not known, or is not from a 
		// starting worker. Something is serious wrong with the system, and 
		// the error should be thrown.
		
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
		             << "Worker Running message from " << Sender.AsString()
								 << " for thread " << Status.ThreadID 
								 << " which does not correspond to a known worker thread";
								 
	  throw std::logic_error( ErrorMessage.str() );
	}
}

//
// In the same way a worker can be blocked by an object while it drains the 
// pending messages. Note that since this message is arriving from an object's
// destructor, it is possible that it may come from a thread that is not a 
// worker, for instance if the scheduled object was directly created in 
// main(). For this reason an unknown thread ID is just ignored.
//

void Theron::Scheduled::WorkerSuspended(
	const Theron::Scheduled::WorkerBlocked & Status, 
	Theron::Actor::Address Sender )
{
	auto FoundWorker = ThreadView.find( Status.ThreadID );
	
	if ( FoundWorker != ThreadView.end() )
		BlockedWorkers.insert( ActiveWorkers.extract( FoundWorker->second ) );
}

// -----------------------------------------------------------------------------
// Object message management
// -----------------------------------------------------------------------------
//
// The dispatcher function first finds the worker to receive the message, 
// and then dispatches as many messages to that worker as possible.

void Theron::Scheduled::DispatchMessages( void )
{
	if ( !MessageQueue.empty() )
  {
		// The worker to handle the first message in the queue can either be one 
		// that already handles the destination object, or if the object is not 
		// currently being handled, a ready worker must exist to process the 
		// message on the given object.
		
		Address TheWorker;
		auto CurrentWorker = ObjectView.find( MessageQueue.front()->To );
		
		if ( CurrentWorker != ObjectView.end() )
			TheWorker = CurrentWorker->second->GetAddress();
		else if ( !IdleWorkers.empty() )
		{
			// The message is s sent to the first worker in the idle set and then 
			// this is moved to the queue of active workers.
			
			auto WorkerReference = *IdleWorkers.begin();
			TheWorker            = WorkerReference->GetAddress();
			
			ActiveWorkers.insert( IdleWorkers.extract( WorkerReference ) );
		}
		else return; // No workers available
		
		// The worker's address is now set to a valid value, and the message(s) can 
		// be dispatched.
		
		ObjectMessage TheMessage;
		
		do
		{
			TheMessage = MessageQueue.front();
			
			Send( QueueLengthControl::ProcessingStart( TheWorker ), 
					  WorkerManager.GetAddress() );
		
			Send( TheMessage, TheWorker );
			
			MessageQueue.pop();
		} 
		while ( (! MessageQueue.empty() ) && 
						(  TheMessage->To == MessageQueue.front()->To ) );
		
		// Then do the housekeeping by adding the object executing on this 
		// worker. 
		
		ObjectView.emplace( TheMessage->To, AddressView.at( TheWorker ) );		
	}
}

// When a scheduled object sends a message to another scheduled object it 
// sends an object message to the scheduler. Three cases exists: The message
// is for an object that already has a message queued on for processing on 
// one of the workers. In this case it is immediately dispatched to that 
// queue; otherwise, if there is a ready worker, the message is dispatched 
// to the first worker in that queue; and finally the message is just added 
// to the queue of messages.

void Theron::Scheduled::NewMessage(
	const Theron::Scheduled::ObjectMessage & TheMessage, 
	Theron::Actor::Address Sender )
{
	MessageQueue.push( TheMessage );
  DispatchMessages();
}

//
// The message done signal is the request from an active worker that it 
// has finished processing the message, and in this case it should receive 
// the message at the front of the queue. If the queue is empty it should 
// be returned to the list of ready workers, and if this list has too many 
// workers, one worker may need to be removed. 
//

void Theron::Scheduled::WorkerDone( 
	const Theron::Scheduled::MessageDone & Status, 
	Theron::Actor::Address Sender )
{
	// The validity of the sender actor is verified. The test should never
	// fail since only workers should send message done messages
	
	auto TheWorker = AddressView.find( Sender );
	
	if ( TheWorker != AddressView.end() )
	{
		// First one record that the processing of this message is completed for
		// this worker.
		
		Send( QueueLengthControl::ProcessingStop( Sender ), 
					WorkerManager.GetAddress() );
	
		// If this worker has more messages queued for the same scheduled object, 
		// there is nothing more to do.
		
		if ( TheWorker->second->GetNumQueuedMessages() > 0 )
			return;
		
		// This worker should be made ready for a new object, and the current 
		// registration of the object ID must be removed. It must exists if the 
		// message was correctly dispatched in the first place.
		
		auto Reference = ObjectView.find( Status.MessageReceiver );
	
		if ( Reference != ObjectView.end() )
			ObjectView.erase( Reference );
		else
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
			             << "The object for which a message was acknowledged, "
									 << Status.MessageReceiver << ", is not registered as "
									 << "an active object for any worker";
									 
		  throw std::logic_error( ErrorMessage.str() );
		}

		// The worker is moved back to the idle worker set.

		IdleWorkers.insert( ActiveWorkers.extract( TheWorker->second ) );
		
		// If there has been a request to remove messages, this can be honoured 
		// now by directly invoking the handler function
		
		if ( WorkersToRemove > 0 )
			DeleteWorker( QueueLengthControl::RemoveWorker(), GetAddress() );
		
		
		// Finally, potentially queued messages can be dispatched.
		
		DispatchMessages();
	}
	else
  {
		// Again an invariant is broken in this case since only workers should 
		// tell the scheduler that it has completed processing a message
		
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
		             << "Message done confirmation received from actor "
								 << Sender.AsString() << " which is not a worker";
								 
	  throw std::logic_error( ErrorMessage.str() );
	}
}

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
// 
// The constructor optionally takes a name of the scheduler, and the watermark 
// value. It starts the queue length controller that starts a number of 
// workers corresponding to the hardware threads available. The watermark value
// is initially set to a queue length of 10 messages per worker, but this 
// is may need to be changed if some knowledge about the application is 
// available

Theron::Scheduled::Scheduled( const std::string Name, 
															Theron::Actor::MessageCount WatermarkValue )
: Actor( Name ), 
  WorkerManager( GetAddress(), WatermarkValue ),
  ActiveWorkers(), BlockedWorkers(), IdleWorkers(),
  ThreadView(), ObjectView(),
  WorkersToRemove(0)
{
	
	// The message handlers are registered first to be able to receive messages
	// from the workers as soon as they are created.
	
	RegisterHandler( this, &Scheduled::NewWorker       );
	RegisterHandler( this, &Scheduled::DeleteWorker    );
	RegisterHandler( this, &Scheduled::WorkerActive    );
	RegisterHandler( this, &Scheduled::WorkerSuspended );
	RegisterHandler( this, &Scheduled::NewMessage      );
	RegisterHandler( this, &Scheduled::WorkerDone      );	
}
