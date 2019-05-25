// UNDER CONSTRUCTION
/*=============================================================================
  Wall Clock Event

  There are situations where real time has to be used for the events, i.e. when 
  events happens at certain wall clock epochs. In the Theron framework, one
  can use a receiver to suspend execution until a message is received for this
  receiver, but there is no way to suspend an actor until a given epoch is 
  passed.
  
  In principle there is no difference between wall clock events and discrete 
  events. The discrete event handler basically implements the following work
  flow:
  1) An actor sets an event by sending its epoch to the handler
  2) The handler stores the event in the pending event queue, and 
  3) When the actor owning the event at the current time has processed its 
     actions, it acknowledges the event.
  4) When the acknowledgement is received, the handler dispatches the next 
     event in the queue (and deletes the previous event), and the time 
     jumps to the current event time.
  
  Wall clock events will differ in that step 2) will correspond to setting up
  a timed wait for the wall clock to reach this epoch. When this wait times out,
  the event will be dispatched as before, and the next timed wait will be set 
  up when the event handling has been acknowledged. Hence, the only modification
  necessary is
  a) To catch the dispatch event and set up the timed wait for the first 
     event time.
  b) Do the real dispatching when the timed wait expires.
  
  The wall clock event class is therefore derived from the event handler. The 
  only changed mechanism the changed dispatch mechanism, and the timed wait 
  mechanism. Concurrency is challenging as we cannot block the Event Handler  
  actor since it must be able to respond to new event requests arriving or 
  acknowledgements of executing events. This implies that the wait must be
  kept apart from the event handler actor itself. 
 
  The solution implemented here is to have a dedicated actor waiting for the 
  time out to expire. In principle, the waiting actor will receive the time to
  wait for, and then sleep for the number of seconds from now until the time 
  out expires, and then send a wake-up message back to the event handler. The
  idea would then be to use a secondary thread to wait for the time 
  out, and have this thread call back to the event handler actor when the wait 
  is over. 
  
  If a new event is requested for an epoch that is earlier than the time out 
  of the current next event, we could simply create a new time out waiting 
  thread for this earlier event. The problem is that we do not know how many 
  times this exceptional situation occurs. The performance impact can thus be 
  severe, and in the worst case exhaust the available resources. The solution 
  to this potential problem is to use normal thread signalling in terms of a 
  mutex protected condition variable that can wake up the waiting thread before 
  the time out epoch.
   
  Author: Geir Horn, University of Oslo, 2016-2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef WALL_CLOCK_EVENT_HANDLER
#define WALL_CLOCK_EVENT_HANDLER

// The time out waits are based on waiting for a condition variable that locks 
// on a mutex, and we will therefore need some thread headers.

#include <mutex>
#include <condition_variable>
#include <thread>

// The interface will also relate to the utilities of the time libraries of 
// the standard library

#include <ctime>
#include <chrono>

// The string stream class will be used for error formatted exception error 
// reporting

#include <string>
#include <sstream>

// Actor framework and extensions

#include "Actor.hpp"
#include "StandardFallbackHandler.hpp"

// And it is derived from the Event Handler

#include "EventHandler.hpp"

namespace Theron
{

/*=============================================================================

 Wall Clock Event

=============================================================================*/
//
// This implementation uses the system clock's time point as the event time 
// type.

template< class EventMessage >
class WallClockEvent 
: public virtual Actor,
  public virtual StandardFallbackHandler,
  public virtual EventData,
  public virtual EventClock< std::chrono::system_clock::time_point >,
  public virtual EventHandler< std::chrono::system_clock::time_point >,
  public virtual DiscreteEventManager< std::chrono::system_clock::time_point, 
																		   EventMessage >
{
public:
	
	using EventTime = std::chrono::system_clock::time_point;
	
private:
  
	using EventManager = DiscreteEventManager< EventTime, EventMessage >;

  // ---------------------------------------------------------------------------
  // Managing the wait for event thread
  // ---------------------------------------------------------------------------
  //	
  // The core mechanism is a thread started to wait for the event epoch. 
  // In order to make this thread wait, we need a time out mutex and 
  // a condition variable that will wait for the time out.
  
  std::mutex 		    		  TimeOutGuard;
  std::condition_variable TimeOutEvent;
 
  // It maintains a separate thread for the actual wait so that Theron can 
  // continue to work normally.
  
  std::thread TheNextEvent;
	
  // This thread execute a function that locks the guard and sets up a wait
  // on the condition variable. When the time out occurs it calls the clock 
	// updating function, and dispatches the next event.
  
  void Wait( void )
  {
		 std::unique_lock< std::mutex >  WaitLock( TimeOutGuard );

		 // We will only update the clock and dispatch the event if the reason for 
		 // waking up is a time out. If the thread is being signalled for any other 
		 // reason, it will just do nothing. 

		 if ( TimeOutEvent.wait_until( WaitLock, NextEventTime() ) 
																						    == std::cv_status::timeout )
	   {
			 UpdateNow();
			 DispatchEvent();
		 }
  }

  // There is a small utility function used to terminate the current wait 
  // (if there is an active wait). It basically checks if the thread is active 
  // (joinable) and if so it notifies the thread and waits for it to terminate.
  
  void TerminateWait( void )
  {
    if ( TheNextEvent.joinable() )
    { 
			// The notification requires a lock (undocumented) and a standard 
			// guard variable is used. It is encapsulated in a separate code block
			// to ensure that it is released before we start waiting for the 
			// thread to terminate.
			
			{
			  std::lock_guard< std::mutex > Guard( TimeOutGuard );
			  TimeOutEvent.notify_one();
			}
			TheNextEvent.join();
    }
  }

  // ---------------------------------------------------------------------------
  // Queueing events
  // ---------------------------------------------------------------------------
  //
  // When events are enqueued, a check is made if the event time is legal 
  // i.e. larger than "Now", i.e. the current system clock plus an offset of 
  // one second for setting up threads etc. If the given event time is not 
  // less than this, it will be enqueued with this time "Now". If the 
  // event time is before the currently first event, it is necessary to 
  // terminate the waiting thread and start a new one for the received event
  // time
  //
  // Need to figure out how time can run backwards...
  
  virtual void EnqueueEvent ( const typename EventManager::Event & TheEvent, 
											        const Theron::Address RequestingActor )
	{
		EventTime ReleaseTime( TheEvent.TimeOfEvent );
		
		if ( ReleaseTime <= std::chrono::system_clock::now() 
															 + std::chrono::seconds(1) )
			ReleaseTime = std::chrono::system_clock::now() + std::chrono::seconds(1);

		// Next the event receiver provided in the event message must be valid. If
		// it is not given, then the actor sending the event request is used as the 
		// event receiver.
		
		Address EventReceiver( TheEvent.EventReceiver );
		
		if ( EventReceiver == Address::Null() )
			EventReceiver = RequestingActor;
		
		// Special care must be taken if the release time is less than the event 
		// time of the first event in the event queue. Then the thread waiting for
		// the currently first event must be terminated, then the new event enqueued
		// to take the first position in the queue, and the thread must be restarted
		// for this event time. 
		//
		// If the new event is not a new first event, it is simply enqueued.
				
		if ( ReleaseTime < NextEventTime() )
		{
			TerminateWait();
			QueueEvent( ReleaseTime, EventReceiver, TheEvent.Message );
			TheNextEvent = std::thread( &WallClockEvent::Wait, this );
		}
		else
			QueueEvent( ReleaseTime, EventReceiver, TheEvent.Message );			
	}
  
  // ---------------------------------------------------------------------------
  // Completed events
  // ---------------------------------------------------------------------------
  //
  // When the event is completed an acknowledgement should be returned, and 
  // the wait for the next event time started provided that the next event time
  // is in the future. This condition cannot be guaranteed as the processing 
  // of the previous event has taken some time, and the real time clock may 
  // have passed the time of the next event. There is nothing to do about this 
  // situation except to dispatch immediately the next event. 
  
	virtual void CompletedEventHandler( const EventCompleted & Ack, 
																			const Address EventProcessor )
	{
		CompletedEvent( EventProcessor, Ack.GetStatus() );
		
		if ( NextEventTime() <= std::chrono::system_clock::now() )
		{
			UpdateNow();
			DispatchEvent();
		}
		else
			TheNextEvent = std::thread( &WallClockEvent::Wait, this );
	}
  
protected:

  // ---------------------------------------------------------------------------
  // Constructor and destructor 
  // ---------------------------------------------------------------------------
  // An interesting point about constructing this event handler is the validity 
  // of its 'this' pointer. The 'this' pointer is a parameter to the nested 
  // actor waiting for the time out events, and it is initialised in the object
  // initialisation list before the constructor body is entered. According to 
  // the good discussion at 
  // http://stackoverflow.com/questions/5058349/is-it-safe-to-use-the-this-pointer-in-an-initialization-list
  // this is OK provided that the pointer is not used to access uninitialized
  // elements of the handler class. The waiting actor's constructor uses the
  // pointer to access only the framework of the actor class which has been 
  // fully constructed and initialised by the base class event handler. Hence,
  // the passing of the 'this' pointer is safe in this setting.
  
public:
  
  WallClockEvent( Theron::Framework & TheFramework, 
								  std::string name = "WallClockHandler" )
  : Actor( TheFramework, name.empty() ? nullptr : name.data() ), 
    StandardFallbackHandler( TheFramework, GetAddress().AsString() ),
    EventData(), 
    EventClock< EventTime >( &CurrentTime ),
    EventHandler( TheFramework, GetAddress().AsString() ),
    DiscreteEventManager< EventTime, EventMessage >( TheFramework, 
																										 GetAddress().AsString() ),
	  TimeOutGuard(), TimeOutEvent(), TheNextEvent()
  { }
  
  virtual ~WallClockEvent( void )
  { }
};

}       // End name space Theron
#endif  // WALL_CLOCK_EVENT_HANDLER
