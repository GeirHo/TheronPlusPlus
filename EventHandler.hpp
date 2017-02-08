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

Author and Copyright: Geir Horn, 2013, 2016, 2017
Revision 2017: Major re-factoring
License: LGPL 3.0
=============================================================================*/

#ifndef EVENT_HANDLER
#define EVENT_HANDLER

#include <Theron/Theron.h>		// The actor framework
#include <set>								// The subscribers to the Now() updates
#include <map>								// The time sorted event queue
#include <chrono>							// Time in POSIX seconds
#include <limits>							// To set the end of simulation at infinitum
#include <string>							// Text strings
#include <memory>							// For smart pointers

namespace Theron
{

/*=============================================================================

 Event Handler

=============================================================================*/
//
// The event handler basically deals with the event time:
//
// 	 1. It ensures that all actors needing the current event time gets it
//	 2. It ensures that there is one and only one event handler in the system
//
// To satisfy the first task it has a set of addresses that each must provide 
// ha message handler to receive the event time. Such an object will be defined
// later in this file. Then it provides two procedures to allow these objects 
// to register and de-register.
//
// Uniqueness is enforced by a flag to indicate if an event handler object has 
// already been created, and by the constructor throwing a standard logic 
// error on the attempt to construct a second event handler.
//
// Furthermore, the event handler is the actual actor, and defines abstract 
// dispatch functions that derived event manager must provide. A derived 
// manager must also be able to return the time of the first event, i.e. the 
// time now.

class EventHandler : public Actor
{
  // ---------------------------------------------------------------------------
  // Managing the current event time 
  // ---------------------------------------------------------------------------

public:
	
  // The time to call the events are defined by the Event Time type
  // which is for the current implementation simply an unsigned long

  using EventTime = std::chrono::seconds::rep;
		
protected:
	
	// The time of the next event must be provided by the derived queue 
	// manager.
	
	virtual EventTime NextEventTime( void ) = 0;
	
	// It is however necessary to know the current time which must be static 
	// in order to be used in the static functions.
	
	static EventTime CurrentTime;
	
	// This time can be read by other parts of the program simply by checking 
	// the now function. However, there is a separate class below that can be 
	// used to wait for the event clock to advance.
	
public:
	
	inline static EventTime Now( void )
	{
		return CurrentTime;
	}
	
private:
	
	// There is a flag to ensure that there is only one event handler
	
	static EventHandler * TheEventHandler;
	
  // All the subscribed Now objects are kept in a simple set since the 
  // addresses have to be unique. It is static because is is accessed by 
	// static functions, and there is only one event handler in the system.
	
	static std::set< Address > NowSubscribers;
	
	// The functions to register and de-register actors subscribing to the changes
	// in the event time. Note that these are private as only the Now object 
	// defined later will be allowed to access these.

public:
		
	static EventTime RegisterTimeObject( const Address & Subscriber )
	{
		NowSubscribers.insert( Subscriber );
		return CurrentTime;
	}
 
  static void DeRegisterTimeObject( const Address & Subscriber )
	{
		NowSubscribers.erase( Subscriber );
	}
 
  // ---------------------------------------------------------------------------
  // Event management 
  // ---------------------------------------------------------------------------
		
	// When the dispatched event is completed, the actor processing the event 
	// must send back a message to acknowledge this to allow the event manager 
	// to move to the next event in the queue. This class only carries the status
	// of the event processing, which is default by definition.
	
	class EventCompleted
	{
	public:
		
		enum class Outcome
		{
			Success,
			Failure
		};
		
	private:
		
		Outcome Status;
		
		// This status field can be read only by the Event Handler class
		
		friend class EventHandler;
		
	public:
		
		EventCompleted( Outcome TheResult = Outcome::Success )
		{
			Status = TheResult;
		}
		
	};

protected:
	
	// A derived event manager will be passed the status in the completed event 
	// feedback function. This function is supposed to delete the event (at least
	// when it was successfully processed), so that a subsequent call on the 
	// current event time will result in the time of the next event to be 
	// processed
	
	virtual void CompletedEvent( Address EventConsumer,  
															 EventCompleted::Outcome Status ) = 0;
	
	// The next event will then be dispatched by a method of the derived event 
	// queue manager. Note that all the now subscribers have been notified before
	// the event is dispatched.
	
	virtual void DispatchEvents( void ) = 0;
	
private:
	
	// The handler for the completed events will first ask the derived class to 
	// clean the event, then dispatch the time of the next event to all actors 
	// needing to know the current event time, before the next event is dispatched
	
	void CompletedEventHandler( const EventCompleted & Ack, 
															const Address EventProcessor );
	
public:
	
	// There is a static function to obtain the address of the event handler 
	// actor so that events can be sent to it without having direct access to 
	// the event handler object.
	
	inline static Address GlobalAddress( void )
	{
		return TheEventHandler->GetAddress();
	}
	
	// The constructor register the handler and the pointer to the instantiation 
	// of this class, or potentially throws the standard logic error if there 
	// is already an event handler registered.
	
	EventHandler( Framework & ExecutionFramework, 
								const std::string & HandlerName = std::string() );
	
	// The destructor is virtual to ensure that the event handler is properly 
	// removed. After destruction another discrete event handler system may be 
	// constructed.
	
	virtual ~EventHandler();
};

/*=============================================================================

 Discrete Event Manager

=============================================================================*/
//
// The event handler has one instantiation as a discrete event manager. This 
// maintains a queue of events where each event holds a message that will be 
// transferred back to the actor setting up the event. The actor must provide 
// handler for this message type as the one to be invoked when the event time 
// is due.
//
// Since the message type to transfer is application dependent, this class is 
// a template on the message type, and provides a handler for the event
// message that should contain the time of the event, the message to return for 
// the event and the address of the receiving actor. The last field can be 
// omitted, which makes the event manager assumed that the event message should 
// be sent back to the actor setting the event.
	
template< class EventMessage >
class DiscreteEventManager : public EventHandler
{
public:

  // ---------------------------------------------------------------------------
  // Events 
  // ---------------------------------------------------------------------------
  //
  // The events are held in a map structure where the sorting key is the 
  // event time which then identifies which actor to call at that time and 
	// the message to send to that actor. A shorthand exists allowing the 
	// address to be omitted in which case the event is sent back to the actor
	// submitting the event

  class Event
  {
	private:
		EventTime 	 TimeOfEvent;
		Address	  	 EventReceiver;
		EventMessage TheMessage;
		
		// These fields will be directly accessed by the event manager when 
		// the event message is received and enqueued.
		
		friend class DiscreteEventManager< EventMessage >;
		
	public:
		
		Event( const EventTime & TheTime, const EventMessage & MessageToSend,
					 const Address TheReceiver = Address::Null() )
		: TimeOfEvent( TheTime ), EventReceiver( TheReceiver ), 
		  TheMessage( MessageToSend )
		{ }
	};

	// Since the map will use the event time as the key, the receiver and 
	// the event message must be kept as the data field in the map. However,
	// this pair should not be publicly available
	
private:

  class EnqueuedEvent
  {
	public:
		
		Address 		 EventReceiver;
		EventMessage Message;
		
		EnqueuedEvent( const Address & TheReceiver, 
									 const EventMessage & TheMessage  )
		: EventReceiver( TheReceiver ), Message( TheMessage )
		{ }
	};

  // Then the structure holding the events can be defined as a time sorted 
	// map sorted on the event times. It is a multi-map since it is perfectly 
	// possible that two events may occur at the same time.

  std::multimap< EventTime, EnqueuedEvent > EventQueue;
	
	// In order to iterate over these events, a shorthand for the iterator 
	// is needed
	
	using EventReference 
				= typename std::multimap< EventTime, EnqueuedEvent >::iterator;

protected:
  
  // Derived classes will need to know if the queue is empty and the time  
  // of the first event and the current time.

  inline bool PendingEvents ( void )
  {
    return !EventQueue.empty();
  }
  
  virtual EventTime NextEventTime ( void )
  {
    return EventQueue.begin()->first;
  }
  
  // ---------------------------------------------------------------------------
  // Event dispatch
  // ---------------------------------------------------------------------------
  //    
  // Time in a discrete event simulation advances from time tick to 
  // time tick, where the time ticks are the points in time where one or
  // more of the actors will have something to do. By definition, everything
  // happening at the same time tick will happen in parallel. Therefore,
  // all events registered with the same time will be dispatched at the 
  // same time.

  // Since a standard pair object is used the "first" field is the event
  // time stamp and the "second" field is the enqueued event of the actor 
  // invoked by this event.
  
  virtual void DispatchEvents ( void )
  {
		if ( !EventQueue.empty() ) 
		{
		  auto CurrentEvent = EventQueue.begin();
		  
		  // We only need to do something if the time of the current event
		  // is larger than the current time, i.e. the event processing has
		  // moved to the next time tick.
		  
		  if ( CurrentEvent->first >= CurrentTime )
		  {
		      // Update the wall clock to this new time tick time.
		    
		      CurrentTime = CurrentEvent->first;
		      
		      // Finally, we can ask the objects owning the events at this time 
		      // to start processing by sending the event message.
		      
		      do
		      {
						Send( CurrentEvent->second.Message, 
									CurrentEvent->second.EventReceiver );
						++CurrentEvent;
		      }
		      while ( ( CurrentEvent != EventQueue.end() ) && 
						      ( CurrentEvent->first == CurrentTime ) ); 
		      
		  };  
		};
  };

  // ---------------------------------------------------------------------------
  // Setting up events
  // ---------------------------------------------------------------------------
  //    
  // An actor enqueues an event by sending the event message to this manager.
  // The following handler receives the event message and enqueues it after 
  // checking that it will not make time run backwards. If the event is equal 
  // to the current time, it will immediately be dispatched.
    
private:
	
  void EnqueueEvent ( const Event & TheEvent, 
							        const Theron::Address RequestingActor )
  {
		if ( CurrentTime <= TheEvent.TimeOfEvent )
		{
			Address EventReceiver;
			
			if ( TheEvent.EventReceiver == Address::Null() )
				EventReceiver = RequestingActor;
			else
				EventReceiver = TheEvent.EventReceiver;
			
		  EventQueue.emplace( TheEvent.TimeOfEvent, 
													EnqueuedEvent( EventReceiver, TheEvent.TheMessage ) );
			
			if ( CurrentTime == TheEvent.TimeOfEvent )
			  Send( TheEvent.TheMessage, EventReceiver );
		}
  };

  // ---------------------------------------------------------------------------
  // Deleting events
  // ---------------------------------------------------------------------------
  //    
	// When an event is acknowledged as completed, the completed event method 
	// will be called. It will identify the event among the events with the 
	// current time that has the given receiving actor as destination, and then 
	// erase that event. If there were several events being dispatched at they 
	// same time, they will be acknowledged one by one and deleted one by one.
	// This rule is respected even if multiple of the events at the same time are
	// for the same actor - it must acknowledge their processing one by one.
	//
	// The outcome of the event processing received is currently just ignored, but
	// failed events could be dispatched again.
	//
	// It should be noted that some external actor must tell the event queue when
	// to start dispatching events. Starting automatically on the first event 
	// received, may not be correct if other and earlier events were to be 
	// received in the initiation phase. In a typical simulation one would 
	// generate a set of initial events before starting dispatching and consuming
	// the events that again generate new events. Instead of providing a special
	// message and hander for this first time start, it is assumed that a first 
	// acknowledgement is sent, either from a real actor or from a Null address. 
	//
	// The current time is initialised to zero. From the enqueue event handler 
	// above it is clear that if one of the initial events occurs at time zero,
	// it will immediately be dispatched, and when it it acknowledged, the events
	// for the next time stamp will be dispatched. If the earliest time stamp of 
	// the initial events occurs later than zero, a dedicated acknowledge must 
	// be sent to start the event queue processing. 
	
protected:
	
	virtual void CompletedEvent( Address EventConsumer,  
															 EventCompleted::Outcome Status )
	{
		std::pair< EventReference, EventReference > 
			EventRange = EventQueue.equal_range( CurrentTime );
			
		for ( EventReference SearchedEvent  = EventRange.first;
										     SearchedEvent != EventRange.second; ++SearchedEvent ) 
			if( SearchedEvent->second.EventReceiver == EventConsumer )
			{
				EventQueue.erase( SearchedEvent );
			  break;
			}
	}

  // ---------------------------------------------------------------------------
  // Termination event
  // ---------------------------------------------------------------------------
  //    
	// A fundamental issue in all simulations where actors are interacting and 
	// setting events for each other is to determine when the game is over. The 
	// main method has to wait for all events to be handled, or alternatively 
	// until a specific time is reached. The first case correspond to the latter 
	// if the specific time is infinitum. In other words, when there are no more 
	// events to process, it is safe to terminate.
	//
	// This can therefore be solved by a standard receiver owning an event for the 
	// specific time. However, what type of message will it receive? It is not 
	// possible to define this without knowing the template parameter of the event
	// queue manager.
  // 
  // The correct class definition is therefore made when the message type is 
  // known
	
	class TerminationEventReceiver : public Receiver
	{
	private:
		
		void TerminationHandler( const EventMessage & FinalEvent,
														 const Address TheEventDispatcher )
		{	}
		
	public:
		
		TerminationEventReceiver( void ) : Receiver()
		{
			RegisterHandler( this, &TerminationEventReceiver::TerminationHandler );
		}
	};

	// Then there is a generator function for this receiver taking the time 
	// to wait for as parameter, and returning a smart pointer to the 
	// termination event receiver. A smart pointer is used so that the receiver 
	// is properly destroyed when it goes out of scope. An event is set for this
	// receiver at the time given as input using the default constructor for the 
	// event message, which should exist.
	
public:
	
	std::shared_ptr< Receiver > GetTerminationObject( EventTime TimeToStop 
																		 = std::numeric_limits< EventTime >::max() )
	{
		std::shared_ptr< Receiver > 
			TheObject = std::make_shared< TerminationEventReceiver >();
			
		EventQueue.emplace( TimeToStop, 
							 EnqueuedEvent( TheObject->GetAddress(), EventMessage() ) );
		
		return TheObject;
	}
	
  // ---------------------------------------------------------------------------
  // Constructor and destructor
  // ---------------------------------------------------------------------------
  //    
  // The constructor register the message handler for queuing events.

  DiscreteEventManager( Framework & TheFramework, 
										    std::string name = std::string() ) 
    : EventHandler( TheFramework, name ),
      EventQueue()
  {
		RegisterHandler(this, &DiscreteEventManager< EventMessage >::EnqueueEvent );
  };
    
  // The destructor simply clears whatever unhanded events there might
  // be left in the queue, and forgets the time subscribers. It must be 
  // virtual since the handler has virtual functions, and other classes 
  // can derive from it.
  
  virtual ~DiscreteEventManager()
  {
    EventQueue.clear();
  };
};

/*=============================================================================

 Now - An object to provide the current event time

=============================================================================*/

// An interesting point in distributed computing is synchronisation. 
// The event will trigger the actor(s) receiving it, but other actors 
// might be triggered by messages these actors generate. Then they might
// want to know the current event time. Polling the time would not 
// necessarily be a good strategy, since it is better if they are able to
// subscribe to the current time. This is what the class Now offers. It is 
// an actor that can be instantiated in other actors (in their framework).
// The event handler maintains a list of the remote objects, and these
// will be updated when the next event is dispatched.
//
// Having this as a receiver has the added benefit that it is possible for an 
// actor to wait for the next time step, if that would be needed.

class Now : public Receiver
{
private:

	// The time the class receives from the event handler

	EventHandler::EventTime CurrentTime;

	// The function to receive the current time from the event handler and 
	// record it

	void ReceiveTime ( const EventHandler::EventTime & TheTime,
									   const Theron::Address TheEventQueue )
	{
    CurrentTime = TheTime;
	};

public:

	// The constructor register the handler for the 

	Now ( void ) 
	: Receiver()
	{
	    RegisterHandler( this, &Now::ReceiveTime );
	    CurrentTime  = EventHandler::RegisterTimeObject( GetAddress() );
	};

	// The current time can be read by using the object as a functor.

	EventHandler::EventTime operator () (void)
	{
    return CurrentTime;
	};

	// When the object is destroyed it will de-register with the global
	// event handler to avoid that it continues to receive the event
	// times that will be left unhanded.

	~Now ()
	{
    EventHandler::DeRegisterTimeObject( GetAddress() );
	};

}; // End of class Now

}       // End name space Theron
#endif  // EVENT_HANDLER
