/*==============================================================================
Scheduled

The actor model is a model for concurrency and when it was conceived the CPUs
were single threaded and the only way to do context switching was to implement 
quasi parallelism by interrupt handlers. This led to the common misunderstanding
that actors should be executed by some scheduler on a set of worker threads. 

This contradicts the design philosophy of Theron++ that uses real threads for 
the actors. A scheduler will cause many problems with respect to priorities of 
the actors, and the order of execution. Furthermore, a scheduler will typically 
run the actors in a pool of threads, say, 4 threads. Seen from the operating 
system and from the CPU these are just 4 threads out of perhaps several 
hundred threads running concurrently, and they will consequently receive their 
fair share of the CPU (say 4/100 = 4%) but these 4% should be shared by all 
actors! In Theron++ the OS scheduler knows about all actors so each actor gets 
the same amount of CPU as they deserve (seen from the OS). By default Linux 
supports more than 100 000 threads (and this number can be increased), so it 
is not a restriction if the actor system is properly modelled.

Actor is an object, but not all objects should be modelled as Actors! The actor 
model is a way to ensure safe concurrency and actors are objects that should 
need to do concurrent activities. Otherwise they are just objects (that may be 
managed by actors). A typical example of an abuse of the actor model is the 
benchmark were the actors form a "ring": Actor A sends a message to Actor B 
that sends a message to Actor C... and then finally Actor Z sends a message 
to Actor A. This should be modelled as a ring of objects where an executor 
gives the command to execute each object (to avoid a deep stack of each object 
directly calling the next object) since the objects are not running 
concurrently!

However, it seems to be hard for many to understand that an object is not an 
actor, and they consistently look for actor systems that support a scheduler on 
a set of workers. Hence this implementation to give an example of how this 
can be done, or if you are just lacy and want a scheduled object, it can 
readily be reused. This implementation provides the following objects:

1) Scheduler, which is a real actor
2) Worker, which is a real actor and some are created by the scheduler
3) Object, which is the scheduled object supporting basic actor functionality 
   of message passing, but is run by the worker.

Only the object can be constructed as the first Scheduled::Object that is 
constructed will create the scheduler, and the last Scheduled::Object will 
delete the scheduler that will again delete its workers.

The scheduling policy is restricted by the simple invariant that an actor should
not close before emptying its message queue. Applying the same principle for 
scheduled objects is more difficult. In Theron++ an actor has a thread for 
processing the message queue, and if the destructor of an actor is invoked, 
the actor can simply wait for the thread to finish processing the messages 
before continuing. There is no risk of any deadlock with other threads. 

In the scheduled world, and object execute in a worker thread. Consider the 
situation where Scheduled::Object A creates Scheduled::Object B in a message 
handler. Now A owns B, but there is no way this ownership can be made explicit 
since the creation will call the constructor of B, just as if B was constructed 
independently of A. One could always provide a dedicated constructor for 
objects own by other objects, but as there is no way to enforce the use of such 
a constructor, it may or may not be used and it is better to let it be. 

A little later, A and B runs, and A gets a message indicating that B is no 
longer needed, and wants to deallocate B. However, B has outstanding messages. 
This means that the message handler of A executes in a worker, say W1, and on 
the stack it places the call for the destructor of B, which on the other hand
needs to wait in the destructor until all messages queued for B have been fully
processed. We are living in a multi-threaded and parallel world, so B simply 
blocks in its destructor waiting for all messages to be processed, and then 
closes and return control to A. Simple, no?

If A and B happens to have messages scheduled for the same worker, W1, then 
the blocking in B's destructor would block the message handler of A that will 
block the worker W1. Then that worker cannot process the messages of B, and 
the processing will deadlock. Thus, the messages for the owned object B must 
be processed on a different worker than its parent object A, and problem solved.

However it is not that easy! First, we do not know that A owns B, and secondly,
B may create another Scheduled::Object C, and messages for C cannot be sent to 
W1 because if B has to wait for C to empty its queue, the system will once again
deadlock.

The solution is that the scheduler keeps only one queue of pending messages for 
all objects, and does not dispatch a message before a worker is free, or if the 
message at the head of the queue is for a Scheduled::Object that is already 
executing in a worker, i.e. if W1 is running A, then other messages to A can
safely be queued for processing by W1. If A blocks W1 in the process of 
destroying B, it will not matter because B will not have any messages queued 
for W1, but perhaps for W2. Then if B blocks W2, it does not matter since 
either W1 or W2 will have messages for C. 

Yes, it is possible that all workers may be stalled if there is a deep ownership
tree to be closed. Note that there is no problem if A wants to close B and B 
has no pending messages. Then B can safely close and A will not block its 
worker. Still it could be a very special situation where all the workers will 
be blocked, and in this situation the scheduler may need to create extra 
workers until the situation resolves. 

The drawback with this is that the worker threads have to wait for the next 
message to execute from the scheduler, and in order to keep all hardware cores 
busy it would be good to have more workers than cores so that some workers can 
run while others wait for their next message.

A second consequence is that the system is taken to be an M/M/W system 
with a Markov arrival process (Poisson) and Markov service time distribution 
and w workers. This situation is known as an Erlang-c model In standard 
notation lambda is the average arrival time and mu is the average service 
time. The first requirement for the system to have a bounded queue is that 
the utilisation factor is less than unity

rho = lambda / (W * mu) < 1

This constraint can be used to decide the number of workers needed. Obviously,
the number of workers should be kept minimal, and the utilisation close to 
unity. However, the constraint only guarantees that the queue size is bounded,
but the average queue length can still become significant. The average queue 
length is given by 

rho * C(W, lambda/mu) / (1-rho) + W*rho

and the real control of the number of workers is done to keep this average 
below a user defined watermark given as a parameter to the scheduler 
constructor.The probability that an arriving package will join the queue 
is given by Erlang's C-formula

C(W, lambda/mu ) = 1/(1+(1-rho)*(W!/(W*rho)^W)Sum_{k=0}^{W-1} (W*rho)^k/k!)

The details about the implementation is further described below.

The Gnu Multi Precision (GMP) library is used and one should therefore link 
code built with scheduled objects with the necessary libraries

-lgmpxx -lgmp

Author and Copyright: Geir Horn, 2018
License: LGPL 3.0
==============================================================================*/

#ifndef THERON_SCHEDULED
#define THERON_SCHEDULED

#include <string>             // Standard strings
#include <memory>             // For smart pointers
#include <mutex>              // To protect the object registry.
#include <unordered_map>      // To convert object IDs to pointers
#include <sstream>            // Nicely formatted error messages
#include <stdexcept>          // Standard exceptions
#include <list>               // For the list of handlers
#include <atomic>             // For the message queue counter
#include <condition_variable> // For signalling when a queue is empty
#include <set>                // To store workers by activity state
#include <map>                // Views on workers.
#include <type_traits>        // For interesting meta-programming
#include <algorithm>          // To operate on the STL containers
#include <queue>              // Scheduler's queue of object messages
#include <chrono>             // To log service and message arrivals

#include <cmath>              // Mathematical functions
#include <gmpxx.h>            // To support very long integers

#include "Actor.hpp"          // Theron++ actor framework

namespace Theron 
{

// All classes are encapsulated in the Scheduled class 
	
class Scheduled : public Actor
{		
	// A scheduled object needs to have an identification object. For this demo 
	// it is defined as a standard string, but it can be any class.
public:
	
	using ObjectIdentifier = std::string;
	
	// The worker class must be forward declared since it will be a friend of the
	// object class and then it must be known.
	
private:
	
	class Worker;
	
	/*============================================================================

  Messages

  ============================================================================*/
  //
	// ---------------------------------------------------------------------------
  // Executed on objects
  // ---------------------------------------------------------------------------
  //
	// The messages of a scheduled object may be of any class, but when it is 
	// sent to other scheduled objects it will be wrapped as a generic message 
	// that will be dynamically cast to the expected message type before invoking 
	// the message handler.
	
	class GenericWrapper
	{
	public:
		
		const ObjectIdentifier To, From;
		
		GenericWrapper( const ObjectIdentifier & TheSender, 
										const ObjectIdentifier & TheReceiver )
		: To( TheReceiver ), From( TheSender )
		{ }
		
		GenericWrapper() = delete;
		
		// The message needs to be polymorphic for the dynamic cast to work, and 
		// even though there is no reason to have a virtual destructor it 
		// sets up the virtual function table allowing run-time identification of 
		// the message type.
		
		virtual ~GenericWrapper( void )
		{ }
	};
	
	// The actual message is a shared pointer to a generic wrapper object
	
	using ObjectMessage = std::shared_ptr< GenericWrapper >;

	// The actual message is a template class keeping a smart pointer to the real 
	// message copy. 
	
	template< class RealMessage >
	class MessageWrapper : public GenericWrapper
	{
	public:
		
		const std::shared_ptr< RealMessage > TheMessage;
		
		// The constructor creates a deep copy of the given message and sets 
		// up a shared pointer to this copied message. Note that this requires 
		// that every message to be sent has a copy constructor.
		
		MessageWrapper( const ObjectIdentifier & TheSender, 
										const ObjectIdentifier & TheReceiver, 
									  const RealMessage & GivenMessage  )
	  : GenericWrapper( TheSender, TheReceiver ), 
	    TheMessage( std::make_shared< RealMessage >( GivenMessage ) )
		{ }
		
		MessageWrapper( void ) = delete;
		
		// The message must have a copy constructor
		
		MessageWrapper( const MessageWrapper & OtherMessage )
		: GenericWrapper( OtherMessage.From, OtherMessage.To ),
		  TheMessage( OtherMessage.TheMessage )
		{ }
		
	};
	
	// When a message has been successfully handled by the object, the worker 
	// sends back a message to inform the scheduler that it is done and that it 
	// is ready to receive the next message. The message carries the receiver 
	// of the done message for easy lookup in the scheduler.
	
	class MessageDone
	{ 
	public:
		
		const ObjectIdentifier MessageReceiver;
		
		MessageDone( const ObjectIdentifier & TheReceiver )
		: MessageReceiver( TheReceiver )
		{ }
		
		MessageDone( void ) = delete;
	};
	
	// ---------------------------------------------------------------------------
  // Thread status
  // ---------------------------------------------------------------------------
  //
	// First the scheduler will send a message to each worker to know its thread 
	// id so that it knows when an object blocks the worker or restarts it.
	
	class GetThreadID
	{	};
	
	// The worker should then just respond with the indication that the thread is 
	// running. 
	
	class WorkerStatus
	{
	public:
		
		const std::thread::id ThreadID;
		
		WorkerStatus( const std::thread::id & TheThreadID )
		: ThreadID( TheThreadID )
		{ }
		
		WorkerStatus( const WorkerStatus & Other )
		: ThreadID( Other.ThreadID )
		{ }
		
		WorkerStatus( void ) = delete;
	};
	
	// The message for indicating a running worker is just a version of worker status
	
	class WorkerRunning : public WorkerStatus
	{
	public:
		
		WorkerRunning( const std::thread::id & TheThreadID )
		: WorkerStatus( TheThreadID )
		{ }

		WorkerRunning( const WorkerRunning & Other )
		: WorkerStatus( Other )
		{ }
		
		WorkerRunning( void ) = delete;
	};

	// When a worker is blocked by an object that is terminating waiting for its 
	// message queue to be serviced, it informs the scheduler that the worker is 
	// blocked.
	
	class WorkerBlocked : public WorkerStatus
	{
	public:
		
		WorkerBlocked( const std::thread::id & TheThreadID )
		: WorkerStatus( TheThreadID )
		{ }
		
		WorkerBlocked( const WorkerBlocked & Other )
		: WorkerStatus( Other )
		{ }
		
		WorkerBlocked( void ) = delete;
	};
	
	/*============================================================================

    Scheduled Object

  ============================================================================*/
  // 
  // The scheduled object is the only object that can be instantiated. It has 
  // certain features similar to an actor, although it does not have a message 
  // queue and it does not have a thread to execute 

public:
	
	class Object
	{
		// -------------------------------------------------------------------------
	  // Scheduler
	  // -------------------------------------------------------------------------
	  //
		// The objects will need to send generic message wrappers to the scheduler,
		// ad since the object is not an actor, it must directly enqueue the message 
		// with the scheduler, and it therefore needs to maintain a pointer to the 
		// scheduler
		
	private:
				
		static std::shared_ptr< Scheduled > TheScheduler;
		
		// There is a public function to initiate this, very similar to the function 
		// to make a shared pointer, and it should be used if a non standard 
		// scheduler is to be used or if one wants to set the parameters of the 
		// scheduler. Note that the scheduler must be created before the execution 
		// of the scheduled objects starts. Since this could potentially start from 
		// the first object's constructor, one would normally expect that the 
		// scheduler is created before the first object.
		
	public:
		
		template< class SchedulerClass, class... SchedulerArguments >
		static std::enable_if_t< std::is_base_of< Scheduled, SchedulerClass >::value >
		MakeScheduler( SchedulerArguments... ArgumentValues )
		{
			TheScheduler = std::make_shared< SchedulerClass >( ArgumentValues... );
		}
				
		// -------------------------------------------------------------------------
	  // Static registry
	  // -------------------------------------------------------------------------
	  //		
		// The objects need to keep track of valid IDs of other objects, and map
		// these to object pointers. Since the objects are created in multiple 
		// threads, store and delete operations on this map must be protected by 
		// a mutex.
		
	private:
		
		static std::mutex RegistryLock;
		
		static std::unordered_map< ObjectIdentifier, Object * > TheRegistry;
		
		// The object must obviously remember its own ID
		
	protected:
		
		const ObjectIdentifier ID;
		
		// In order to make the object as similar to the actor as possible,
		// it provides a get address function returning the ID.
		
	public:
		
		inline ObjectIdentifier GetAddress( void )
		{ return ID; }
		
		// There is a utility function to construct an ID if the object should have
		// an automatic ID. it simply constructs a string where the object number
		// is added to the standard object name as a string. It is a minor problem 
		// with this approach since two objects could be constructed by two threads
		// at the same time and then be given the same ID. The way to fix this 
		// is that the make ID creates a dummy registration which is then 
		// subsequently updated to point at this object.
		
	private:
		
		inline ObjectIdentifier MakeID( void )
		{
			static unsigned long int ObjectCounter = 0;
			
			std::ostringstream Name;
			std::lock_guard< std::mutex > Lock( RegistryLock );
			
			Name << "Scheduled::Object(" << ++ObjectCounter << ")";
			
			TheRegistry.emplace( Name.str(), nullptr );
			
			return ObjectIdentifier( Name.str() );
		}

		// -------------------------------------------------------------------------
	  // Pending messages
	  // -------------------------------------------------------------------------
	  //
	  // As discussed above, the object should respect the invariant that all 
	  // messages pending handling should be consumed before terminating, and 
	  // this necessitate having a counter for pending messages. This must be 
	  // atomic as it is increased by the sender of the message that can execute
	  // in another thread than the object receiving the message.
	  
	  std::atomic< MessageCount > PendingMessages;
		
		// Again the value of this can be checked in a way similar to the one used
		// used for actors
		
	public:
		
		inline MessageCount GetNumQueuedMessages( void ) const
		{ return PendingMessages.load(); }
		
		// To block the destructor until all messages have been handled, 
		// a condition variable is needed. It is locked on the registry lock 
		// to prevent new messages to be sent while the actor is de-registering.
		
	private:
		
		std::condition_variable QueueEmpty;
		
		// The message handler should decrement the number of pending messages when
		// the message has been fully handled. The specific handler class is 
		// therefore declared as a friend. This could have been avoided by a 
		// public function, but then the handler would have had a problem if the 
		// hander function had been declared as private. With the friend declaration 
		// it works for both situations.
		
		template< class ObjectType, class MessageType >
		friend class SpecificHandler;

		// -------------------------------------------------------------------------
	  // Message management
	  // -------------------------------------------------------------------------
	  //
		// Sending messages is relatively easy. The sender function first verifies
		// that the requested destination object does exist, and if it does 
		// creates a wrapper object for the message before sending it to the 
		// scheduler using the scheduler's own send function. A standard runtime 
		// error is thrown if the receiver object does not exist or its pointer 
		// is invalid. Before sending the message to the scheduler, the number of 
		// pending messages for the receiver is increased to ensure that the 
		// receiving object will not terminate before this message has been 
		// serviced

	protected:
		
		template< class MessageType >
		inline bool Send( const MessageType & GivenMessage, 
											const ObjectIdentifier & TheReceiver ) const
		{
			// It is necessary to lock the registry to ensure that the object 
			// to receive the message is not destructing and the reference to 
			// the object remains valid 
			
			std::lock_guard< std::mutex > Lock( RegistryLock );
			
			// Then one can safely look up the receiver and know that it is 
			// available to receive messages if it is found.
			
			auto ReceiverObject = TheRegistry.find( TheReceiver );
			
			if ( ( ReceiverObject != TheRegistry.end() ) && 
				   ( ReceiverObject->second != nullptr ) )
			{
				ReceiverObject->second->PendingMessages++;
				
				ObjectMessage TheMesssage = 
				std::make_shared< MessageWrapper< MessageType > >( ID, TheReceiver, 
																													 GivenMessage );
				
				TheScheduler->Send( TheMesssage, Address(), 
														TheScheduler->GetAddress() );
			}
			else 
			{
				std::ostringstream ErrorMessage;
				
				ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
				             << "Scheduled object " << ID 
				             << " tries to send a message of type "
										 << typeid( MessageType ).name()
										 << " to a schedule object that does not exist"
										 << TheReceiver;
										 
			  throw std::logic_error( ErrorMessage.str() );
			}
			
			return true;
		}

		// Receiving messages is more complicated because a message handler must be
		// registered, and the list of message handlers must exist. The version 
		// provided here is fundamentally identical to the implementation for 
		// real actors in order to keep the API identical.The core idea is that
		// there is a generic handler that is able to handle a generic message.
		// This handler returns true if the message could be handled, otherwise 
		// it returns false
		
	private:
		
		class GenericHandler
		{
		public: 
			
			virtual bool ProccessMessage( 
			        const ObjectMessage & ReceivedMessage ) = 0;
							
			// There is also a function to mark the message as handled. Essentially 
			// this simply decreases the pending message counter for the object. This 
			// can not be done by the process message function since there could be 
			// multiple handlers registered for a single message type and all must 
			// be executed before the message is marked as done.

			virtual void MessageDone( void ) = 0;
		};
		
		// Pointers to these handlers are kept in a simple list, and although 
		// they are not supposed to be shared with other objects, they 
		// needs to be converted to pointers to specific types, which gives 
		// a second pointer temporary to the same handler, and thus a 
		// unique pointer is not the right approach.
		
		std::list< std::shared_ptr< GenericHandler > > MesssageHandlers;

		// It will be the worker asked to process a given message that will traverse 
		// the list of handlers, and ask each in to process the arrived message and 
		// it should therefore be allowed to access this list of handlers.

		friend class Worker;
		
		// A particular handler is a function on a particular instance of this 
		// object type. Hence both the actor type and the message type the function 
		// can receive needs to be stored. It must be protected because a derived 
		// class will implicitly construct these objects when it registers its 
		// message handlers.
		
		template< class ObjectType, class MessageType >
		class SpecificHandler : public GenericHandler
		{
		private:
			
			// The object pointer is explicitly stored
			
			ObjectType * TheObject;
			
			// The signature of the handler function is as for the actors: Message 
			// and sender ID
			
			void (ObjectType::*HandlerFunction)( const MessageType &, 
																					 const ObjectIdentifier );
			
			// The constructor takes the values for these as argument and stores 
			// them for future use.

		public:
			
			SpecificHandler( ObjectType * OwningObject, 
		  void (ObjectType::*TheHandler)( const MessageType &, const ObjectIdentifier ))
			: GenericHandler(), TheObject( OwningObject ), 
			  HandlerFunction( TheHandler )
			{ }
			
			// There is also a copy constructor just to ensure that it works well 
			// with the container.
			
			SpecificHandler( const SpecificHandler & Other )
			: GenericHandler(), TheObject( Other.TheObject ),  
			  HandlerFunction( Other.HandlerFunction )
			{ }
			
			SpecificHandler( void ) = delete;
			
			// The main element is the message processing function. It first checks 
			// that the message is of the right type, and if it is, the handler will 
			// be called with the correct message type. If it cannot be converted 
			// the function will return false.
			
		private:
			
			virtual bool ProccessMessage( 
      const ObjectMessage & ReceivedMessage ) override
      {
				std::shared_ptr< MessageWrapper< MessageType > > RealMessage = 
					std::dynamic_pointer_cast< MessageWrapper< MessageType > >( 
						ReceivedMessage );
					
				if ( RealMessage )
				{
					(TheObject->*HandlerFunction)( *(RealMessage->TheMessage), 
																				   RealMessage->From );					
					return true;
				}
				else
					return false;
			}
			
			// If at least one of the handlers confirm that the message has been 
			// processed, the worker thread can mark it as done and decrement the 
			// pending message counter of the concerned object.
			
			void MessageDone( void ) override
			{ 
				TheObject->PendingMessages--;	
				
				if ( TheObject->PendingMessages.load() == 0 )
				  TheObject->QueueEmpty.notify_all();
			}
		}; // End class specific handler
		
		// The handler functions are typically registered by the constructor of the 
		// object and the registration function just links in a specific handler 
		// object for the given function and message type. Note that multiple 
		// handlers can be defined for the same message.

	protected:
		
		template< class ObjectType, class MessageType >
		inline bool RegisterHandler( ObjectType * const TheObject, 
	  void (ObjectType::*TheHandler)( const MessageType & , ObjectIdentifier ) )
		{
			MesssageHandlers.emplace_back( 
				std::make_shared< SpecificHandler< ObjectType, MessageType > >( 
					TheObject, TheHandler ) );
			
			return true;
		}
		
		// Removing a handler is easier for scheduled objects than for actors since 
		// it does not matter if the handler is running when deleted since the 
		// deletion simply removes the specific handler(s) defined for a particular
		// message.
		
		template< class ObjectType, class MessageType >
		inline bool DeregisterHandler( ObjectType * const TheObject, 
	  void (ObjectType::*TheHandler)( const MessageType & , ObjectIdentifier ) )
		{
			MesssageHandlers.remove_if( 
				[&]( const std::shared_ptr< GenericHandler > & Possibleandler )->bool{
				  auto CandidateHandler = std::dynamic_pointer_cast< 
					  SpecificHandler< ObjectType, MessageType > >( Possibleandler );
						
					if ( CandidateHandler 
								&& ( CandidateHandler->TheObject == TheObject )
						    && ( CandidateHandler->HandlerFunction == TheHandler ) )
						return true;
					else
						return false;
			});
			
			return true;
		}

		// -------------------------------------------------------------------------
	  // Construction
	  // -------------------------------------------------------------------------
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
		
		Object( std::string Name = std::string() );

		// -------------------------------------------------------------------------
	  // Destruction
	  // -------------------------------------------------------------------------
	  //
	  // If there are messages pending processing when the destructor is called,
	  // processing should continue as normal until there are no more messages 
	  // and then the ID of this object will be removed so no further messages 
	  // can be sent to this object. It will inform the scheduler when the 
	  // thread blocks and unblock. There is a small problem that the queue can 
	  // drain, but a new message created before the address has been removed.
		
		virtual ~Object( void );
		
	}; // End scheduled object
	
	/*============================================================================

    Queue length control

  ============================================================================*/
  // 
  // Computing the expected queue length is a relatively costly operation and 
  // it should to the least possible extent infer with the execution of objects 
  // getting the messages. The computation can also run in parallel with the 
  // execution of the objects, and it is therefore implemented as a separate 
  // actor created by the scheduler.
  //
  // The service time for a message to an object is counted from the time the 
  // scheduler dispatches the message to a worker until the worker's message 
  // that the processing is done. The scheduler simply sends these two epochs 
  // of the system clock to the controller. The controller will then compute 
  // the average service time mu.
  // 
  // In the same way it sends the epoch of the system clock when a message is 
  // queued for an object. The controller will then compute the inter-arrival 
  // time for messages, the lambda.
  //
  // The expected queue length is computed every time a message has been 
  // serviced, and if it exceeds the watermark threshold, a request is sent 
  // to the scheduler to create another worker. If the watermark threshold 
  // is met also with one less worker, a request is sent to delete an worker.
  
private:
	
  class QueueLengthControl : public Actor
  {
		// -------------------------------------------------------------------------
	  // Messages
	  // -------------------------------------------------------------------------
    //
		// There are commands to create workers or delete workers sent to the
		// scheduler.
		
	public:
		
		class CreateWorker {};
		class RemoveWorker {};
		
		// There are two messages for the service time: When it starts and when it 
		// is completed. They will both be of the type time stamp.
		
	private:
		
		class TimeLog
		{
		public:
			
			const std::chrono::system_clock::time_point TimeStamp;
			
			TimeLog( void )
			: TimeStamp( std::chrono::system_clock::now() )
			{}
		};
		
		// The messages indicating that a worker is starting to process a message
		// and end the processing does contain the worker's address in order to 
		// match the stop time against the start time per worker.
		
	public:
		
		class ProcessingStart : public TimeLog
		{
		public:
			
			const Address WorkerAddress;
			
			ProcessingStart( const Address & TheWorker )
			: TimeLog(), WorkerAddress( TheWorker )
			{ }
			
			ProcessingStart( void ) = delete;
		};
		
		class ProcessingStop : public TimeLog
		{
		public:
			
			const Address WorkerAddress;
			
			ProcessingStop( const Address & TheWorker )	
			: TimeLog(), WorkerAddress( TheWorker )
			{ }
			
			ProcessingStop( void ) = delete;
		};
		
		// There is also a message to be sent every time an object message 
		// arrives to detect the inter-arrival times
		
		class ObjectMessageArrival : public TimeLog
		{	
		public:
			
			ObjectMessageArrival( void ) : TimeLog() 
			{}
		};

		// -------------------------------------------------------------------------
	  // Data structures
	  // -------------------------------------------------------------------------
    //
		// The number of workers is simply the number of time a create worker 
		// command has been sent minus the number of times a remove worker command 
		// has been sent. Note that this assumes that the worker creation is 
		// instantaneous. This commits an error, but it is likely less than the 
		// error made by assuming that the inter-arrival times are independent and
		// markovian. The size of this type is taken to be the same as the maximum
		// index type that can be used for a vector, see below.

	private:
		
		using WorkerCounterType = std::vector< mpz_class >::size_type;
		
		WorkerCounterType NumberOfWorkers;
		
		// The watermark value for the maximum allowed average queue length is
		// remembered. The total messages in the system is also computed by 
		// increasing a counter when a message arrives and decrease the counter 
		// when a message is serviced
		
		std::queue< ObjectMessage >::size_type Watermark, MessagesInTheSystem;
		
		// To compute the average object message inter-arrival time a total 
		// inter-arrival duration must be accumulated, and the last message arrival
		// recorded to compute the duration since last message. Furthermore, a 
		// counter for the number of received messages. The number of messages and 
		// the total inter-arrival time can potentially be very large numbers and 
		// the Gnu Multi-Precision library is therefore used to hold these numbers
		// However, there is still an issue that for very large numbers these 
		// quantities just get larger and larger, potentially not changing the 
		// average inter-arrival rate much; this is further discussed in the 
		// serviced message handler below.
		
		mpz_class TotalInterArrivalTime, NumberOfMessages;
		
		// In order to compute the inter-arrival time the last arrival time must be 
		// remembered.
		
		std::chrono::system_clock::time_point LastArrival;
		
		// The average inter-arrival rate is computed when the arrival counters are 
		// updated, and as it will be used in the computations of the expected 
		// queue length it is kept for the arrival of the service time.
		
		mpq_class Lambda;
		
		// The same considerations go for the service time counters. Although the 
		// service rate is a  quotient of these two counters, it will not be 
		// stored because it is updated by the function updating the counters and 
		// immediately used to calculate the expected queue length.
		
		mpz_class TotalServiceTime, MessagesServiced;
		
		// In order to match the service time end with the right start time, the 
		// epoch when the services started on the worker must be remembered. This 
		// is done by a simple multimap since sequences of messages for the same 
		// executable object are immediately queued for the same worker. 
		
		std::multimap< Address, std::chrono::system_clock::time_point > 
		ServiceStartTimes;
		
		// In order to avoid recomputing the factorials involved in the Erlang C
		// function they are stored in a vector. This is the reason for the size of 
		// the number of workers counter. This vector is extended with one element 
		// every time a new worker is added. 
		
		std::vector< mpz_class > WorkerFactorials;

		// -------------------------------------------------------------------------
	  // Message handlers
	  // -------------------------------------------------------------------------
    //
		// The handler recording object message arrival times is slightly 
		// complicated since it also computes the inter-arrival time and adds this 
		// to the global counters.
		
		void RecordArrivalTime( const ObjectMessageArrival & TheSignal, 
														Address Sender );

		// The signal that message for an executable object has been allocated to a 
		// worker and thus its processing started stores the entry in the service 
		// start time map. 
		
		void RecordStartTime( const ProcessingStart & TheSignal, Address Sender );
				
		// Ending the processing of an object message will first compute the 
		// service time duration and update the counters and remove the remembered
		// start time, similar to the arrival time handler. However, it will then 
		// re-assess the number of workers by computing the expected queue size
		// for the current number of workers. If this is larger than the watermark,
		// an additional worker is requested. If it is less than the threshold, 
		// the expected queue size is computed for one worker less. If this is 
		// still less than the watermark threshold, the scheduler is requested to 
		// remove one worker.
		
		void RecordProcessingTime( const ProcessingStop & TheSignal, 
															 Address Sender );
		
		// -------------------------------------------------------------------------
	  // Constructor
	  // -------------------------------------------------------------------------
    //
		// The constructor takes the watermark value and the address of the 
		// scheduler actor and asks it to create the number of workers supported by 
		// the current hardware 
		
	public:
		
	  QueueLengthControl(  
	    const Address & SchedulerActor,
		  std::queue< ObjectMessage >::size_type QueueLengthThreshold,
			std::string Name = std::string() );
		
		// The destructor simply ensures that the actor is properly deleted
		
		virtual ~QueueLengthControl( void )
		{	}
		
 } WorkerManager; // Actor Queue Length Control

	/*============================================================================

    Worker

  ============================================================================*/
  // 
	// The main functionality of a worker is to get a message wrapper, look up 
	// the corresponding object and try to deliver a message to one of its 
	// handlers. If no handler exists for the message, a logic error is thrown.
	// After successfully handling a message, a worker will send back a message 
	// to the scheduler that will dispatch the next message in its queue to the 
	// worker. 
	
	class Worker : public Actor
	{
		// -------------------------------------------------------------------------
	  // Message handlers
	  // -------------------------------------------------------------------------
	  //
		// The main handler is the one that respond to message wrapper from an object
		// and will try to deliver this to the handlers in sequence until all 
		// handlers have been tried. All handlers must be tried since multiple 
		// handlers can be registered for a message type.
		
	private:
		
		void ProcessMessage( const ObjectMessage & TheMessage, 
												 Address TheDispatcher );
		
		// There is a simple utility handler to respond the thread ID of this worker
		// when the Scheduler asks after starting the worker.
		
		void ReportThread( const GetThreadID & TheRequest, Address TheDispatcher );

		// -------------------------------------------------------------------------
	  // Constructor
	  // -------------------------------------------------------------------------
	  //
	  // The worker simply requires an actor ID name which is assigned by the 
	  // scheduled object, and register the two message handlers.
		
	public:
		
	  Worker( const std::string & Name = std::string() );
		
		// And the destructor is virtual because the actor's destructor is virtual
		
		virtual ~Worker( void )
		{ }
	}; // End class Worker
	
	/*============================================================================

    Managing workers

  ============================================================================*/
  // 
  // An iterator to these elements is the way to reference a worker.
																				 
  using WorkerReference = std::shared_ptr< Worker >;
																				 
  // Messages are dispatched to non-blocked workers in two situations: When a 
	// message arrives from a scheduled object, and when a worker reports that 
	// its message has been served.
	// 
	// The workers can be in one of three states:
	// 		 Active  - Working on a message, not ready for next message
	//     Blocked - Blocked on current message, not ready for next message
	//     Idle    - Idle worker ready for next message
	//
	// There is one set for each of these states, and the objects are moved 
	// among the lists when they change state, but without invalidating iterators
	// to the workers.
	
	std::set< WorkerReference > ActiveWorkers, 
														  BlockedWorkers, 
															IdleWorkers;
																				 
  // There are three views (maps) on the set of workers. One by thread ID, one 
  // by the object ID of the receiver of a message sent to the worker, and one 
	// by the worker's address.
																				 
  std::map< std::thread::id,  WorkerReference > ThreadView;
	std::map< ObjectIdentifier, WorkerReference > ObjectView;
	std::map< Address, WorkerReference >          AddressView;
	
  // This will send messages to create or delete workers. Creating a new worker 
	// is just to insert a new worker into the list of blocked workers, and ask 
	// this newly created worker to register its thread.
	
	void NewWorker( const QueueLengthControl::CreateWorker & Command, 
									Address Sender );
	
	// Deleting a worker is more difficult since only passive workers can be 
	// removed. If there are no workers in the ready list when a worker should 
	// be removed, a counter is incremented to indicate the number of workers
	// to remove. Its type is defined in terms of the counter for any of the 
	// worker lists.
	
	std::set< WorkerReference >::size_type WorkersToRemove;
	
	// The message handler can therefore be invoked in a response from the worker
	// manager or, in the interest of not duplicating code, called by the message
	// handler for completed object messages, i.e. when a worker has finished its 
	// current work and could be removed. 
	
	void DeleteWorker( const QueueLengthControl::RemoveWorker & Command, 
										 Address Sender );
	
protected:
	
	// ---------------------------------------------------------------------------
  // Execution state
  // ---------------------------------------------------------------------------
  //
	// The change in the execution state is depending on the worker status 
	// messages. These can come from the workers or from objects that need to 
	// block their worker while waiting for their message queue to drain. The 
	// fist handler receives the information that the worker is running, and the 
	// state can only be detected from the thread.
	
	void WorkerActive( const WorkerRunning & Status, Address Sender );
	
	// In the same way a worker can be blocked by an object while it drains the 
	// pending messages. Note that since this message is arriving from an object's
	// destructor, it is possible that it may come from a thread that is not a 
	// worker, for instance if the scheduled object was directly created in 
	// main(). For this reason an unknown thread ID is just ignored.
	
	void WorkerSuspended( const WorkerBlocked & Status, Address Sender );

	/*============================================================================

  Message management

  ============================================================================*/
  //
  // The messages received from the scheduled objects are kept in a simple 
  // queue if they cannot be directly delivered to a ready worker.
  
	std::queue< ObjectMessage > MessageQueue;
	
	// The dispatcher function takes the first message in the queue and dispatches
	// these according to the three cases possible: The message is for an object 
	// that already has a message queued on for processing on one of the workers. 
	// In this case it is immediately dispatched to that  queue; otherwise, if 
	// there is a ready worker, the message is dispatched to the first worker 
	// in that queue; and finally the message is just left in the queue of 
	// messages.
	
	void DispatchMessages( void );
  
	// When a scheduled object sends a message to another scheduled object it 
	// sends an object message to the scheduler. Fundamentally, it just calls 
	// the dispatcher function.
	
	void NewMessage( const ObjectMessage & TheMessage, Address Sender );
	
	// The message done signal is the request from an active worker that it 
	// has finished processing the message, and in this case it should receive 
	// the message at the front of the queue. If the queue is empty it should 
	// be returned to the list of ready workers, and if this list has too many 
	// workers, one worker may need to be removed.
	
	void WorkerDone( const MessageDone & Status, Address Sender );

	/*============================================================================

    Constructor

  ============================================================================*/
  // 
  // The constructor optionally takes a name of the scheduler, and the watermark 
	// value. It starts the queue length controller that starts a number of 
	// workers corresponding to the hardware threads available. The watermark value
	// is initially set to a queue length of 10 messages per worker, but this 
	// is may need to be changed if some knowledge about the application is 
	// available
	//
	// The constructor cannot be protected in this context because the scheduler
	// may be initialised by the schedule object constructor crating a pointer 
	// to the object, and then it must be accessible via a pointer. The same 
	// goes for the situation where one would like to use the function to directly
	// create the scheduler with different parameters. A further explanation can
	// be found at Stack Exchange 
	// https://stackoverflow.com/questions/2393325/why-is-protected-constructor-raising-an-error-this-this-code 
	
public:
	
	Scheduled( const std::string Name = std::string(), 
	  MessageCount WatermarkValue = 10 * std::thread::hardware_concurrency() );

  // The destructor should not need to do any special actions as the standard 
	// containers will themselves clean up the objects when they delete.
	
	virtual ~Scheduled( void )
	{ }

};     // End class Scheduled
}      // End namespace Theron
#endif // THERON_SCHEDULED
