/*=============================================================================
Event Handler

This class implements an event queue for Actors under the Theron actor 
framework. It maintain a list of events sorted on occurrence, and then sends an
message to the actors whose events are at the head of the list. When the actor 
acknowledges the event, the next event is deleted, and if the event at the 
head of the queue has a different activation time all the events for that time
are dispatched.

Note also that the event handler ignores what action the actor does for an 
event, so in is possible to register several events for the same actor at 
the same time - the actor will have to handle this situation.

Author: Geir Horn, 2013, 2016
Lisence: LGPL 3.0
=============================================================================*/

#ifndef EVENT_HANDLER
#define EVENT_HANDLER

#include <Theron/Theron.h>
#include <set>
#include <map>
#include <ctime>

namespace Theron
{

class EventHandler : public Theron::Actor
{
public:

    // The time to call the events are defined by the Event Time type
    // which is for the current implementation simply an unsigned long

    typedef std::time_t EventTime;
    
    // The events are held in a map structure where the sorting key is the 
    // event time which then identifies which actor to call at that time.

    typedef std::pair< EventTime, Theron::Address > Event;

    // An interesting point in distributed computing is synchronisation. 
    // The event will trigger the actor(s) receiving it, but other actors 
    // might be triggered by messages these actors generate. Then they might
    // want to know the current event time. Polling the time would not 
    // necessarily be a good strategy, since it is better if they are able to
    // subscribe to the current time. This is what the class Now offers. It is 
    // an actor that can be instantiated in other actors (in their framework).
    // The event handler maintains a list of the remote objects, and these
    // will be updated when the next event is dispatched.

    class Now : public Theron::Receiver
    {
    private:

	// The time the class receives from the event handler

	EventTime CurrentTime;

	// A receiver is, well, a receiver so it is not supposed to send 
	// anything. However, if a Now object goes out of scope and is 
	// destroyed it must be able to tell the event handler that it no
	// longer needs the clock updates. The Send function is only public
	// on the framework, so therefore a pointer to the framework used
	// to create the Now object is cached in order for the destructor to
	// send the message to stop the clock subscription.

	Theron::Framework * TheFramework;
	
	// The address of the event handler is cached in order to send a 
	// message to the event handler when the object is created 
	// (registration) and another message when the now object goes out of 
	// scope.
	
	Theron::Address TheEventHandler;

    protected:

	// The function to receive the current time from the event handler

	void ReceiveTime ( const EventHandler::EventTime & TheTime,
			   const Theron::Address TheEventQueue )
	{
	    CurrentTime = TheTime;
	};

    public:

	// The constructor gets the framework from the actor using the
	// Now object and registers a handler to receive the time update as
	// well as register with the global event handler. The handler will
	// immediately respond with the current time, and it is important to
	// remember that the value should not be used before it has been 
	// updated! The constructor will therefore wait until the first 
	// message arrives, potentially pausing the thread.

	Now ( Theron::Framework & LocalFramework, 
	      const Theron::Address & TheEventQueue 
				      = Theron::Address("EventHandler") )
	: Theron::Receiver(), TheEventHandler( TheEventQueue )
	{
	    RegisterHandler( this, &EventHandler::Now::ReceiveTime );
	    LocalFramework.Send( GetAddress().AsString(), GetAddress(), 
				 TheEventHandler );
	    CurrentTime  = static_cast< EventHandler::EventTime >(0);
	    TheFramework = &LocalFramework;
	    Wait();
	};

	// The current time can be read by using the object as a functor.

	EventTime operator () (void)
	{
	    return CurrentTime;
	};

	// When the object is destroyed it will de-register with the global
	// event handler to avoid that it continues to receive the event
	// times that will be left unhanded.

	~Now ()
	{
	    TheFramework->Send( GetAddress().AsString(), GetAddress(), 
				TheEventHandler );
	};

    }; // End of class Now

private:

    // All the subscribed Now objects are kept in a simple set since the 
    // addresses have to be unique

    std::set < Theron::Address > NowSubscribers;

    // In order to prevent the time from running backwards by
    // allowing events to be set in the past there currently largest event
    // time will be stored between invocations.

    EventTime CurrentEventTime;

    // Then the structure holding the events

    std::multimap < EventTime, Theron::Address > EventQueue;

protected:
  
    // Derived classes will need to know if the queue is empty and the time  
    // of the first event and the current time.
  
    inline bool PendingEvents ( void )
    {
      return !EventQueue.empty();
    }
    
    inline EventTime FirstEventTime ( void )
    {
      return EventQueue.begin()->first;
    }
    
    inline EventTime GetTime ( void )
    {
      return CurrentEventTime;
    }
    
    // Time in a discrete event simulation advances from time tick to 
    // time tick, where the time ticks are the points in time where one or
    // more of the actors will have something to do. By definition, everything
    // happening at the same time tick will happen in parallel. Therefore,
    // all events registered with the same time will be dispatched at the 
    // same time.
  
    // Since a standard pair object is used the "first" field is the event
    // time stamp and the "second" field is the address of the actor 
    // setting this event.
    
    virtual void DispatchEvent ( void )
    {
	if ( !EventQueue.empty() ) 
	{
	  auto CurrentEvent = EventQueue.begin();
	  
	  // We only need to do something if the time of the current event
	  // is larger than the current time, i.e. the event processing has
	  // moved to the next time tick.
	  
	  if ( CurrentEvent->first > CurrentEventTime )
	  {
	      // Update the wall clock to this new time tick time.
	    
	      CurrentEventTime = CurrentEvent->first;
	      
	      // Then update all the remote clock objects with the new time
	      
	      for ( Theron::Address Subscriber : NowSubscribers )
		Send( CurrentEventTime, Subscriber );
	      
	      // Finally, we can ask the objects owning the events at this time 
	      // to start processing by sending the time back to the objects.
	      
	      do
	      {
	      	Send( CurrentEventTime, CurrentEvent->second );
		++CurrentEvent;
	      }
	      while ( ( CurrentEvent != EventQueue.end() ) && 
		      ( CurrentEvent->first == CurrentEventTime ) ); 
	      
	  };  
	};
    };

    // Any actor enqueues an event by sending a time to this queue, and 
    // then it receives a message back with the time when the event is due.
    // The following function receives the queuing event. If this was the 
    // first event inserted, we immediately ask for it to be dispatched.
    // It will not accept events to be set before the current time, 
    // however clock synchronisation is difficult in a distributed system
    // so if the event is requested in the past, the command is understood
    // to be "as soon as possible" and the event is enqueued for the current
    // time.
    //
    // If the event is enqueued for the current time, the requesting actor must
    // be informed that the event is now, and we still have to enqueue it 
    // because it should be taken out of the event queue when the requesting 
    // actor acknowledges that the event handling has taken place.

    virtual void EnqueueEvent ( const EventTime & TheTime, 
			        const Theron::Address RequestingActor )
    {
	if ( CurrentEventTime < TheTime )
	  EventQueue.insert( Event( TheTime, RequestingActor ) );
	else
	{
	  EventQueue.insert( Event( CurrentEventTime, RequestingActor ) );
	  Send( CurrentEventTime, RequestingActor );
	}

	// If this is the only event in the queue is should be immediately
	// dispatched
	
	if ( EventQueue.size() == 1 )
	    DispatchEvent();
    };

private:
  
    // There is a handler for receiving acknowledgements for 
    // dispatched events. If the event is positively acknowledged it is 
    // deleted, otherwise it is simply dispatched again. Since we could have
    // dispatched many events at the same time, we have to search the events
    // having the current time for the one belonging to the requesting actor.
    //
    // It is necessary to be robust and allow a remote actor to acknowledge 
    // already acknowledged events. In other words, we can receive an 
    // acknowledgement for an event which does no longer exist in the 
    // event queue.

    void HandleAcknowledgement ( const bool & Ack,
				 const Theron::Address RequestingActor )
    {
	if ( Ack == true )
	{
	  auto CurrentEvent = EventQueue.begin();
	  
	  while ( ( CurrentEvent != EventQueue.end() ) &&
		  ( CurrentEvent->first == CurrentEventTime ) )
	    if ( CurrentEvent->second != RequestingActor )
	    	++CurrentEvent;
	    else
	    {
	      // We have found what we were looking for, so we delete this
	      // element and then stop the search.
	      
	      EventQueue.erase( CurrentEvent );
	      break;
	    };
	};
	
	DispatchEvent();
    };

    // The event handler can also receive requests from remote Now 
    // objects to subscribe to changes in the event time. Since such objects
    // can subscribe at any time this handler automatically responds back to 
    // the subscribing object with the current time. It should be noted that
    // the handler will delete the subscriber if it receives an address of
    // a currently subscribed Now object. The second address is not needed
    // here as it should be identical to the first address.

    void NowObjectHandler( const std::string & RemoteNowID,
			   const Theron::Address Subscriber   )
    {
	auto ExistingObject = NowSubscribers.find( Subscriber );

	if ( ExistingObject == NowSubscribers.end() )
	{
	    NowSubscribers.insert( Subscriber );
	    Send( CurrentEventTime, Subscriber );
	}
	else
	    NowSubscribers.erase( ExistingObject );
    };

public:

    // The constructor initiates the actor and registers the two message 
    // handlers 

    EventHandler ( Theron::Framework & TheFramework, 
		   const char *const name = "EventHandler" ) 
	    : Theron::Actor( TheFramework, name ),
	      NowSubscribers(), EventQueue()
    {
	RegisterHandler(this, &EventHandler::EnqueueEvent );
	RegisterHandler(this, &EventHandler::HandleAcknowledgement );
	RegisterHandler(this, &EventHandler::NowObjectHandler );

	CurrentEventTime = static_cast<EventHandler::EventTime>(0);
    };
    
    // The destructor simply clears whatever unhanded events there might
    // be left in the queue, and forgets the time subscribers. It must be 
    // virtual since the handler has virtual functions, and other classes 
    // can derive from it.
    
    virtual ~EventHandler()
    {
      EventQueue.clear();
      NowSubscribers.clear();
    };
};

/******************************************************************************
 
 Termination Time
 
 This is given a particular termination time and provides a Wait function 
 that will use a now object to block the current thread until the specified
 time is reached. It does this simply by setting the event time, and wait 
 for the event handler to send this time back when the event is due.
   
 It is also possible to use the same mechanism to implement a termination
 when all the pending events scheduled by other actors have been serviced.
 This is achieved by setting the termination time to infinitum, since the 
 termination event will then be the very last event to execute. This is 
 actually the default behaviour, selected if no termination time is given.
   
 WARNING: The termination object should not be created as the first event
 driven actor! This because if the event list is empty, the event handler will
 immediately dispatch the set event, so termination time is immediately 
 reached, and calling Wait() on this object will have no effect. Furthermore,
 to preserve causality, all events subsequently set will be flushed to this 
 stop time (it should however be harmless since if this Termination Time 
 object is used to block main() from terminating the program, it will not 
 block and the program will terminate before any actor gets any chance to do
 anything.)
 
*******************************************************************************/

#include <limits>

class TerminationTime : public Theron::Receiver
{
protected:
  
  void ReceiveTime ( const EventHandler::EventTime & Now,
		     const Theron::Address TheEventHandler )
  {
    // We simply ignore the message
  };
  
public:
  
    TerminationTime( Theron::Framework &     TheFramework,
		     EventHandler::EventTime StopTime 
		     = std::numeric_limits< EventHandler::EventTime >::max(),
		     const Theron::Address & TheEventHandler 
					     = Theron::Address("EventHandler") )
    : Theron::Receiver()
    {
      RegisterHandler( this, &TerminationTime::ReceiveTime );
      TheFramework.Send( StopTime, GetAddress(), TheEventHandler );
    };
};

}       // End name space Theron
#endif  // EVENT_HANDLER
