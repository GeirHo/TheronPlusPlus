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
     2a) if the event is the only event, it is immediately dispatched back 
         to the owning actor, and the clock moves to this epoch.
  3) When the actor owning the event at the current time has processed its 
     actions, it acknowledges the event.
  4) When the acknowledgement is received, the handler dispatches the next 
     event in the queue (and deletes the previous event), and the time 
     jumps to the current event time.
  
  Wall clock events will differ in that 2a) will correspond to setting up
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
   
  Author: Geir Horn, University of Oslo, 2016
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

#include <iostream> // TEST debugging

// And it is derived from the Event Handler

#include "EventHandler.hpp"

namespace Theron
{

/******************************************************************************
  Class Wall Clock Event
*******************************************************************************/

class WallClockEvent : public EventHandler
{
private:
  
  // ---------------------------------------------------------------------------
  // Waiting actor
  // ---------------------------------------------------------------------------
  // The waiting actor is constructed at the same time as the event handler, 
  // and working in the same Theron framework. It sets up the time out event 
  // based on the received absolute time out time (which must be in the future),
  // and sends this time back to the event handler when the time out occurs.
    
  class WaitForEvent : public Theron::Actor
  {
  private:
    // The waiter needs the address of the wall clock event actor in order 
    // to be able to signal when the event has occurred. 
    
    Theron::Address TheEventHandler;
    
    // The core mechanism is a thread started to wait for the event epoch. 
    // In order to make this thread wait, we need a time out mutex and 
    // a condition variable that will wait for the time out.
    
    std::mutex 		    TimeOutGuard;
    std::condition_variable TimeOutEvent;
   
    // It maintains a separate thread for the actual wait so that Theron can 
    // continue to work as normally.
    
    std::thread TheNextEvent;
    
    // This thread execute a function that locks the guard and sets up a wait
    // on the condition variable. When the time out occurs it reports the time
    // back to the event handler. It terminates immediately if a time point 
    // of the past is given. This situation should trigger an exception at the 
    // event handler class.
    
    void EventThread( const std::chrono::system_clock::time_point & EventEpoch )
    {
      if ( EventEpoch <= std::chrono::system_clock::now() )
	return;
      else
      {
	 std::unique_lock< std::mutex >  WaitLock( TimeOutGuard );
	 EventHandler::EventTime TheTime = std::chrono::system_clock::to_time_t( EventEpoch );
	  
	 // We will only return the event to the event handler if the reason 
	 // for waking up is that the time out occurs. For all other reasons 
	 // nothing will happen.

	 if ( TimeOutEvent.wait_until( WaitLock, EventEpoch ) 
						    == std::cv_status::timeout )
	 {
	   Send( EventEpoch, TheEventHandler );
	   std::cout << "Wall clock event at " << std::ctime( &TheTime );
	 }
	 else
	   std::cout << "Wall Clock wait cancelled for " << std::ctime( &TheTime );
      }
    }

    // There is a small utility function used to terminate the current wait 
    // (if there is an active wait). It can safely be called, and basically 
    // checks if the thread is active (joinable) and if so it notifies the 
    // thread and waits for it to terminate.
    
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
    
    // The actor has a message handler that receives an event time. This time 
    // is converted to a proper time point, and then we will set up a wait 
    // for this time point as soon as the thread has stopped executing. Note 
    // that if this function is called, it is already detected that the time 
    // for the first time out has changed, and we should abort the wait by 
    // notifying the waiting thread.

    void SetTimeOut( const EventHandler::EventTime & TheTime,
		     const Theron::Address ClockEventHandler )
    {
      std::chrono::system_clock::time_point EventEpoch 
			  = std::chrono::system_clock::from_time_t( TheTime );
      
      // The new time is less than the current time out epoch if we have an 
      // active wait, and we must therefore first terminate the wait.
      
      TerminateWait();
      
      // Then the next waiting thread can be created to wait for the time of 
      // the next event.
      
      TheNextEvent = std::thread(&WaitForEvent::EventThread, this, EventEpoch);
std::cout << "Wall clock event created for " << std::ctime( &TheTime );      
    }

  public:

    // The constructor attaches this actor to the same framework as its owning
    // event handler, and stores the address of the handler for sending back 
    // time out messages.

    WaitForEvent( WallClockEvent * TheOwner )
    : Actor( TheOwner->GetFramework() ), 
      TheEventHandler( TheOwner->GetAddress() ),
      TimeOutGuard(), TimeOutEvent()
    { 
      RegisterHandler( this, &WaitForEvent::SetTimeOut );
    }
    
    // If this object is destroyed while it is waiting for a time out, this is 
    // a logical error, but as a mitigation action we must cancel the pending 
    // wait. The reason is that if we leave it to time out, the we will send a
    // message later to the owning event handler that has closed with this 
    // object. Notifying the condition variable if we are not waiting is 
    // harmless and has no effect.
    
    ~WaitForEvent( void )
    {
      TerminateWait();
    }
        
  } Waiter; 		    // something that waits and serves the event handler
  
  // ---------------------------------------------------------------------------
  // Message handlers 
  // ---------------------------------------------------------------------------
  // When the event time is received from the waiting actor, we know that a time 
  // out occurred and nothing else cancelled the event, and we can safely 
  // dispatch this event using the standard event dispatching mechanism, which 
  // will update the current event time clock in the process.
  
  void TimeOutReceiver(const std::chrono::system_clock::time_point & EventEpoch,
		       const Theron::Address TheWaiter		    )
  {
    EventHandler::DispatchEvent();
  }
  
  // In a discrete event simulation time cannot move backwards because the 
  // current event time is assumed to be the present wall clock time, so the 
  // default behaviour of the enqueue event message handler is to truncate 
  // any events before the current event time to the current event time, and 
  // enqueue the event as another event for the current time. When the events 
  // are real time epochs, other tasks can be done while an actor waits for its 
  // events, and these tasks may lead to other timed events being defined. 
  // However, in this case the new events could be prior to the event time we 
  // already wait for, and we need to signal the waiting actor that it can take 
  // down the current time out and then send it the new first event time to 
  // wait for.
  
  virtual void EnqueueEvent( const EventTime & TheTime, 
			     const Theron::Address RequestingActor )
  {
    // Setting an event which is prior to the real time now is clearly an error
    // which we do not know how to handle.
    
    EventTime Now = std::chrono::system_clock::to_time_t(
				 std::chrono::system_clock::now() );
    
    if ( TheTime <= Now )
    { 
      std::ostringstream ErrorMessage; 
      
      // The standard function ctime produces the time in a human readable 
      // form, but it adds a new line character to the string produced. This 
      // must be trimmed before the time can be reported.
      
      std::string TimeString( std::ctime( &TheTime ) );
      
      TimeString.resize( TimeString.size() - 1 );
      
      ErrorMessage << "Given event time " << TimeString
		   << " is less than the wall clock time Now = ";
		   
      TimeString = std::ctime( &Now );
      TimeString.resize( TimeString.size() - 1 );
      
      ErrorMessage << TimeString << std::endl;
      
      throw std::domain_error( ErrorMessage.str() );
    }
    
    // If the queue of events is not empty, and if the event time requested is
    // less than the time of the event we are currently waiting for, we must
    // cancel the wait and set the new wait time.
    //
    // If there are no other events, then the discrete event hander's enqueue 
    // function will call the dispatch function for the first event enqueued, 
    // and this will lead to setting the time out for that event. 
    
    if ( PendingEvents() && ( TheTime < FirstEventTime() ))
      Send( TheTime, Waiter.GetAddress() );
    
    // Finally, we can safely enqueue the event as one event to wait for using
    // the discrete event handler's enqueue function. Even though this function
    // truncates the event's time to the 'current time' it is harmless since
    // the 'current time', which is the time of the last event occurring, must
    // be less or equal to 'Now' and we have already prevented the time from 
    // running backwards. Hence, the discrete event enqueue function will 
    // simply enqueue the event and call the dispatcher if it is the first 
    // event in the queue.

    EventHandler::EnqueueEvent( TheTime, RequestingActor );
  }

  // ---------------------------------------------------------------------------
  // Dispatcher 
  // ---------------------------------------------------------------------------
  // When the events set for the current time have been dispatched, the handler
  // will wait for the acknowledgements of these events. For each 
  // acknowledgement it will call the dispatch function to move the clock to
  // the next event since it does not know how many acknowledgements to wait 
  // for. Hence, the dispatcher we have to check if it really should set up 
  // a new event (time point), or simply ignore this dispatching request.
  // 
  // However, it can also be that the dispatcher is called when the first event
  // in the queue has been created, still the same condition should hold as 
  // the 'current event time' will be the time of an event executed in the past
  // or initialised to zero.
  //
  // Note that the discrete event dispatching is split into two parts: The 
  // first part sets the time for the waiter, and when the waiter responds 
  // back that the epoch has been reached, the time out receiver actually 
  // dispatches the event time to the waiting actors.
  
protected:
  
  virtual void DispatchEvent( void )
  {
    if ( PendingEvents() && ( GetTime() < FirstEventTime() ))
      Send( FirstEventTime(), Waiter.GetAddress() );
  }

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
		  const char * const name = "EventHandler")
  : EventHandler( TheFramework, name ), Waiter( this )
  {
    // We need to register the time out event handler (call back from the 
    // Waiter), but not the enqueue event handler since this is a virtual 
    // function that has been registered as a handler by the base class.
    
    RegisterHandler( this, &WallClockEvent::TimeOutReceiver );
  }
  
  virtual ~WallClockEvent( void )
  { }
};

}       // End name space Theron
#endif  // WALL_CLOCK_EVENT_HANDLER
