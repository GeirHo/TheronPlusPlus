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

#include <set>													// The subscribers to the Now() updates
#include <map>													// The time sorted event queue
#include <chrono>												// Time in POSIX seconds
#include <limits>												// To set the end time to infinitum
#include <string>												// Text strings
#include <memory>											  // For smart pointers
#include <functional>										// For functions returning Now

#include <type_traits>									// To check a type at compile time

#include <Theron/Theron.h>						  // The actor framework
#include "StandardFallbackHandler.hpp"  // Debugging and error recovery

#include <iostream>

namespace Theron
{

	
/*=============================================================================

 Event Data

=============================================================================*/
//
// The event data basically deals with the event time counter and it ensures 
// that there is one and only one event handler in the system
//
// Uniqueness is enforced by a flag to indicate if an event handler object has 
// already been created, and by the constructor throwing a standard logic 
// error on the attempt to construct a second event handler. Since the actual
// event handler class is a template, its static data is stored in dedicated 
// data class.
//

class EventData
{
protected:
	
	// The Theron address cannot be directly stored since a Theron object cannot 
	// be a static object.
	
	static std::string EventHandlerName;
	
	// The functions to register and de-register actors subscribing to the changes
	// in the event time. Note that these are private as only the Now object 
	// defined later will be allowed to access these.

public:
	
  // There is a static function to obtain the address of the event handler 
	// actor so that events can be sent to it without having direct access to 
	// the event handler object.
	
	inline static Address HandlerAddress( void )
	{
		return Address( EventHandlerName.data() );
	}

  // ---------------------------------------------------------------------------
  // Now management 
  // ---------------------------------------------------------------------------
  //
	// The Chrono library defines two concepts, a time point and a duration. For 
	// real time tracking the time point is connected to a clock that has a time 
	// resolution and a start time (epoch). For real time events this is 
	// sufficient since time will be compared with one of the clocks' 
	// representation of Now. For simulated events, the clock is basically just 
	// a counter. A counter is  also inherent in the Chrono clocks, but those 
	// clocks have a different period. However, when comparing two time points, 
	// it is really their counters that are compared, and they count the ticks of 
	// the clock since the clock's epoch.
	//
	// Consider for instance the POSIX time which counts seconds since first 
	// January 1970, i.e. the epoch of the classical time_t. A given time can be 
	// represented as a counter of seconds, or a duration since first January 
	// 1970, which is also a counter. However, for a clock with microseconds 
	// resolution the counter value for the same time would be different. 
	//
	// The chrono library converts among different time point depending on the 
	// interpretation of the counter. However, for the simulated events, there 
	// might not be a need for interpreting the counter as anything but a counter. 
	// Hence, the time Now is therefore represented as a counter capable of 
	// holding the time point of highest resolution, i.e. nanoseconds in the 
	// chrono library. This is just the representation of the counter. 
	
	// First a standard definition of the type used to represent the time counter
	
	using TimeCounter = std::chrono::nanoseconds::rep;

protected:
	
	// Then the counter representing current time
	
  static TimeCounter CurrentTime;
	
	// There are three functions to convert to the counter depending on the 
	// type of clock representation. The first converter accepts a standard time 
	// point, and simply returns the value since the epoch.
	
	template< class Clock, class Duration >
	inline TimeCounter ToTimeCounter( 
				 const std::chrono::time_point< Clock, Duration > & NowTime )
	{
		return NowTime.time_since_epoch().count();
	}
	
	// The second version is even simpler and accepts a time duration and assumes
	// that the count of ticks in this duration is the counter value.
	
	template< class Representation, class Period >
	inline TimeCounter ToTimeCounter( 
				 const std::chrono::duration< Representation, Period > & Duration )
	{
		return Duration.count();
	}
	
	// The last conversion applies to integral types and simply returns the type
	// itself. It is included for completeness so that it is possible for other 
	// classes just to call the conversion function leaving to the compiler to 
	// use the right version.
	
	template< class Type, typename std::enable_if< 
												std::is_integral< Type >::value, Type >::type = 0 >
	inline TimeCounter ToTimeCounter( const Type & NowTime )
	{
		return NowTime;
	}
	
	// The above counter is intended to be used by the local event handler on 
	// this endpoint. However, if other actors needs to be informed about the 
	// current time, they will subscribe by sending the following class to the 
	// event handler.
	
public:
	
	class SubscribeNowCounter
	{ };
	
	// They can cancel the subscription by sending the following class
	
	class UnSubscribeNowCounter
	{ };
	
	// Since it is important that all the now subscribers are updated with the 
	// next time before the next event is dispatched, they will need to 
	// acknowledge the reception of a new counter value by returning the 
	// following message to the sender of the counter value.
	
	class AcknowledgeNowCounter
	{ };
	
  // ---------------------------------------------------------------------------
  // Event management 
  // ---------------------------------------------------------------------------
  //
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
				
	public:
		
		inline Outcome GetStatus( void ) const
		{
			return Status;
		}
		
		EventCompleted( Outcome TheResult = Outcome::Success )
		{
			Status = TheResult;
		}
	};
	
  // ---------------------------------------------------------------------------
  // Constructor and destructor
  // ---------------------------------------------------------------------------

	EventData( void )
	{ }
	
	~EventData( void )
	{ }
};

/*=============================================================================

	Representation of Now

=============================================================================*/
//
// The now counter will have an interpretation which can be according to a 
// clock's time point or just as a number. This is tricky since there are two 
// radically different interpretations that must be used, and the conversion 
// is in two steps:
//
// The counter is first converted to the internal type used by the event handler
// to represent time, then this is converted to the desired time representation
// In the case that the the internal interpretation is just a counter and the 
// external representation is just a counter, then both conversions will be 
// identity conversions.

template< class EventRepresentation >
class EventClock 
{
private:
	
	// The event clock allows conversion between the raw counter and different 
	// time formats. If the event clock is at the same network endpoint as the 
	// event handler and the event data, it could read the current time counter 
	// directly. However, if the clock is on a remote node, it must get the 
	// counter as a message and maintain a local copy. The counter is therefore
	// kept as a pointer which is de-referenced when it is used.
	
	EventData::TimeCounter * TheCurrentTime;
	
protected:
	
	// If the event representation is integral, a standard conversion is done.
	// Note that this template is only enabled if the actual type to be filled 
	// is an integral type, possibly with a loss of precision
	
	template< class Type, 
	typename std::enable_if< std::is_integral< Type >::value, Type >::type = 0 >
	void ConvertCounter( Type & Result )
	{
		Result = *TheCurrentTime;
	}
	
	// If the type is a time point, then it will be filled assuming that the 
	// counter represents the ticks in the duration from the clock's epoch. Note
	// that the time point to be filled is assumed to be empty
	
	template< class Clock, class Duration >
	void ConvertCounter(std::chrono::time_point<Clock, Duration> & Result)
	{
		Result += Duration( *TheCurrentTime );
	}
	
	// If the event representation is a time point and it should be converted 
	// to a different type of time point, a time point cast will be used
	
	template< class ThisClock, class ThisDuration, class Clock, class Duration >
	void ConvertTime( 
		   const std::chrono::time_point< ThisClock, ThisDuration > & NowTime, 
		   std::chrono::time_point< Clock, Duration > & OutTime )
	{
		OutTime = std::chrono::time_point_cast< Duration >( NowTime );
	}
	
	// Converting to a duration is fundamentally just taking the time from the 
	// epoch of the current time point and use the duration cast to the 
	// destination duration.
	
	template< class ThisClock, class ThisDuration, 
						class Representation, class Period >
	void ConvertTime( 
		   const std::chrono::time_point< ThisClock, ThisDuration > & NowTime, 
		   std::chrono::duration< Representation, Period > & Duration )
	{
		Duration = std::chrono::duration_cast< 
									 std::chrono::duration< Representation, Period > >
										 ( NowTime.time_since_epoch() );
	}
	
	// If the current event time representation is an integral counter value 
	// it can still be converted to one of the other two representations, but 
	// it is desirable to suppress the templates if the event representation is
	// not an integral type. Note that in this case the counter is implicitly 
	// assumed to be of the same resolution as the clock of the requested time 
	// point. It is also assumed that the given time point will be at zero, i.e. 
	// at the clock's epoch.
	
	template< class Clock, class Duration, 
						typename std::enable_if< 
										 std::is_integral< EventRepresentation >::value, 
										 EventRepresentation >::type = 0 >
	void ConvertTime( const EventRepresentation & NowTime, 
									  std::chrono::time_point<Clock, Duration> & OutTime )
	{
		OutTime += Duration( NowTime );
	}
	
	template< class Representation, class Period, 
						typename std::enable_if< 
										 std::is_integral< EventRepresentation >::value, 
										 EventRepresentation >::type = 0 >
	void ConvertTime( const EventRepresentation & NowTime, 
										std::chrono::duration<Representation, Period> & Duration )
	{
		Duration = std::chrono::duration< Representation, Period >( NowTime );
	}
	
	// The identity conversion requires both the event representation and the 
	// given type to be integral, and then simply assigns the now counter.
	
	template< class Type, 
						typename std::enable_if< 
									   std::is_integral< EventRepresentation >::value, 
										 EventRepresentation >::type = 0,
					  typename std::enable_if< 
								     std::is_integral< Type >::value, Type >::type = 0  >
  void ConvertTime( const EventRepresentation & NowTime, Type & OutTime )
	{
		OutTime = NowTime;
	}
	
public:
	
	// The Now function can now be provided as a general template where the 
	// compiler will figure out the right combination of the above conversion 
	// functions based on the provided types.
	
	template< class ReturnType >
	ReturnType Now( void )
	{
		EventRepresentation NowTime;
		ReturnType				  OutTime;
		
		ConvertCounter( NowTime );
		ConvertTime   ( NowTime, OutTime );
		
		return OutTime;
	}
	
	// The constructor takes an address of the time counter to refer to when 
	// providing the conversion of the current time.
	
	EventClock( EventData::TimeCounter * PointerToCounter )
	{
		TheCurrentTime = PointerToCounter;
	}
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
// 
// It takes an address to the event manager in order to subscribe to events 
// when it is created, and un-subscribe when it is deleted.

class NowCounter : public Receiver
{
protected:
	
	// The clock ticks the class receives from the event handler

	EventData::TimeCounter CurrentTime;
	
	// In order to be able to send messages, this counter must have a framework
	// pointer since a receiver does not have any own send function.
	
private:
	
	Framework * TheFramework;

	// In order to be able to cancel the subscription when destructed it is also 
	// necessary to remember the address of the event handler.
	
	Address TheEventManager;
	
	// The function to receive the current time from the event handler and 
	// record it. It also sends back the acknowledgement to the event queue.

	void ReceiveTime ( const EventData::TimeCounter & TheTime,
									   const Theron::Address TheEventClock )
	{
    CurrentTime = TheTime;
		TheFramework->Send( EventData::AcknowledgeNowCounter(), 
												GetAddress(), TheEventClock );
	};

public:

	// The constructor register the handler with the event queue.

	NowCounter ( Framework & TheHost, 
							 const Address TheEventQueue = EventData::HandlerAddress() ) 
	: Receiver(), TheFramework( &TheHost ), TheEventManager( TheEventQueue )
	{
	    RegisterHandler( this, &NowCounter::ReceiveTime );
	    
			TheFramework->Send( EventData::SubscribeNowCounter(), 
													GetAddress(), TheEventManager );
	};

	// The current time can be read by using the object as a functor.

	EventData::TimeCounter operator () (void)
	{
    return CurrentTime;
	};

	// When the object is destroyed it will de-register with the global
	// event handler to avoid that it continues to receive the event
	// times that will be left unhanded.

	~NowCounter()
	{
    TheFramework->Send( EventData::UnSubscribeNowCounter(), 
												GetAddress(), TheEventManager );
	};

}; // End of class NowCounter

// The Now object is a combination of the this receiver with the Event Clock 
// to allow various representations of the counter to align with the event 
// handler

template< class EventRepresentation >
class Now : public NowCounter, public EventClock< EventRepresentation >
{
	friend class EventClock< EventRepresentation >;
	
public:
	
	Now( Framework & TheHost, 
			 const Address TheEventQueue = EventData::HandlerAddress() )
	: NowCounter( TheHost, TheEventQueue ),
	  EventClock< EventRepresentation >( &CurrentTime )
	{ }
};

/*=============================================================================

	Event Handler

=============================================================================*/

// Furthermore, the event handler is the actual actor, and defines abstract 
// dispatch functions that derived event manager must provide. A derived 
// manager must also be able to return the time of the first event, i.e. the 
// time now.
// 
// The event time is assumed to be a time point, but its representation can 
// be changed depending on the desired clock resolution, but it may also be a 
// simple counter.

template< class EventTime >
class EventHandler : public virtual Actor,
										 public virtual StandardFallbackHandler,
										 public virtual EventData,
										 public virtual EventClock< EventTime >
{
  // ---------------------------------------------------------------------------
  // Managing the Now subscribers 
  // ---------------------------------------------------------------------------

private:
	
	// All the subscribed Now objects are kept in a simple set since the 
  // addresses have to be unique. It is static because is is accessed by 
	// static functions, and there is only one event handler in the system.
	
	std::set< Address > NowSubscribers;
	
	// Now receivers will have to send a message to be added to this list of 
	// notifications. The handler for these messages is trivial.
	
	void AddSubscriber( const EventData::SubscribeNowCounter & Message, 
											const Address Subscriber )
	{
		NowSubscribers.insert( Subscriber );
	}

	// A similar handler removes a subscriber from the set
	
	void RemoveSubscriber( const EventData::UnSubscribeNowCounter & Message, 
												 const Address Subscriber )
	{
		NowSubscribers.erase( Subscriber );
	}
	
	// When the event time changes, all subscribers must acknowledge the new time
	// before the next event can be dispatched. It is therefore necessary to wait
	// until all receivers have acknowledged the updated time. In order to wait 
	// for this, a receiver must be deployed.
	//
	// However, the standard Wait function called with the number of messages to 
	// wait for cannot be directly used because typically the event handler will 
	// dispatch the Now counter to all the Now receivers, and then wait for the 
	// return of the acknowledgement. However, some of the Now counter messages 
	// can already have been acknowledged by the time the event handler calls 
	// the Wait function - and it will wait for messages that will never arrive.
	//
	// The solution is to keep a separate counter, and provide a dedicated message
	// handler to receive the initial counter value. Then this counter will be 
	// decreased for each received acknowledgement, and the Wait function will 
	// wait for this counter to be zero before terminating.
	//
	// This means that the Receiver's wait function should be protected from being
	// called directly, and a separate Wait function mus be provided. Furthermore,
	// one may rightly ask why the count of Now receivers is sent as a message and 
	// why not call a method directly to set this value. The reason is that as 
	// a message it can be handled concurrently with the Event Handers continued 
	// processing. However, fundamentally one should never call a method on an 
	// actor directly if it can be avoided. Messages will always be handled in 
	// order, meaning that the value of the counter will be set before any of the 
	// acknowledgements will be processed.
	
	class TimeDistribution : public Receiver
	{
	private:
		
		// The counter for the number of acknowledgements to wait for. This is 
		// defined in terms of the size type of the set container since the 
		// addresses of the Now receivers is held in this container, the number 
		// of acknowledgements to wait cannot exceed the range of this size type.
		
		std::set< Address >::size_type PendingAcknowledgements;
		
		// The handler to receive the number of now subscribers.
		
		void NumberOfNowReceivers( const std::set< Address >::size_type & Number, 
															 const Address TheEventHandler )
		{
			PendingAcknowledgements = Number;
		}
		
		// The handler for acknowledgements simply decreases the counter
		
		void Acknowledgement( const EventData::AcknowledgeNowCounter & Message, 
													const Address Subscriber )
		{	
			PendingAcknowledgements--;
		}
		
	public:
		
		// Since the event handler will need to know the address of this receiver,
		// it must be allowed to call its Get Address function, even though the 
		// class is protected.
		
		using Receiver::GetAddress;
		
		// The wait function will wait for an incoming message, i.e. an 
		// acknowledgement, until the acknowledgement counter has reached zero
		
		void Wait( void )
		{
			while ( PendingAcknowledgements > 0 )
				Receiver::Wait();
		}
		
		// The constructor registers the handlers and ensure that the counter 
		// initially is zero.

		TimeDistribution( void ) : Receiver()
		{
			PendingAcknowledgements = 0;
			
			RegisterHandler( this, &TimeDistribution::NumberOfNowReceivers );
			RegisterHandler( this, &TimeDistribution::Acknowledgement      );
		}
	};
	
	// To avoid keeping it in the same active memory as the event handler, it is 
	// allocated and stored in a shared pointer that ensures proper destruction 
	// of the receiver when the event handler terminates.
	
	std::shared_ptr< TimeDistribution > ConsistentTime;
	
protected:
	
	// There is a method that updates the current time and the Now receivers. It 
	// will wait for the acknowledgement returns from the receivers before 
	// terminating.
	//
	// A word of warning: The current implementation will work fine in the case 
	// of simulated time, i.e. the event time defines the actor system's time 
	// Now. In the case that the events occur at the real time of the system's 
	// clock, one must not use now subscribers since the time distribution 
	// protocol will delay the dispatching of the next event way beyond its now 
	// clock time. In such situation one should rather read the system's clock 
	// value of Now to ensure that the real time is used and not the real time 
	// distributed at some past event epoch.
	
	void UpdateNow( void )
	{
		TimeCounter NextTime  = ToTimeCounter( NextEventTime() );
		
		if ( CurrentTime < NextTime )
		{
			CurrentTime = NextTime;
			
			if ( ! NowSubscribers.empty() )
		  {
				// First the number of Now receivers is sent to the acknowledgement
				// receiver so it knows how many subscribers to wait for
				
				Address AcknowledgementReceiver( ConsistentTime->GetAddress() );
				
				Send( NowSubscribers.size(), AcknowledgementReceiver );

				// Send the current time stamp to all subscribers, making sure that 
				// they will respond back to the acknowledgement receiver. In order to
				// ensure that the whole system has a consistent time, all 
				// acknowledgements must be received before the next event is dispatched
								
				for ( Address NowReceiver : NowSubscribers )
					GetFramework().Send( NextTime, AcknowledgementReceiver, NowReceiver );
				
				ConsistentTime->Wait();
			}
		}
	}
	
	// There is a method to return a function object that is calling Now on the 
	// this event handler. It should only be used on the same network endpoint as
	// this event handler.
	
public:
	
	template< class ReturnType >
	std::function< ReturnType(void) > NowFunction( void )
	{
		return [this](void)->ReturnType{ return Now< ReturnType >(); };
	}
	
	// Alternatively, one can obtain a Now receiver object which will 
	// asynchronously be updated with the counter value, and that will provide 
	// an event clock interface to convert this to whatever value is useful. The 
	// receiver executes in the same framework as the event handler, and so again
	// this requires that the receiver is running on the same network endpoint as
	// this event handler. It returns a shared pointer to the receiver object to 
	// ensure that it is properly de-allocated when it goes out of scope at some
	// point.
	
  std::shared_ptr< Now< EventTime > > NowReceiverObject( void )
	{
		return std::make_shared< Now< EventTime > >( GetFramework(), GetAddress() );
	}
	
  // ---------------------------------------------------------------------------
  // Managing the current event time 
  // ---------------------------------------------------------------------------
	
protected:
	
	// The time of the next event must be provided by the derived queue 
	// manager.
	
	virtual EventTime NextEventTime( void ) = 0;
		
  // ---------------------------------------------------------------------------
  // Interface for derived event managers 
  // ---------------------------------------------------------------------------
	//
	// A derived event manager will be passed the status in the completed event 
	// feedback function. This function is supposed to delete the event (at least
	// when it was successfully processed), so that a subsequent call on the 
	// current event time will result in the time of the next event to be 
	// processed
	
	virtual void CompletedEvent( Address EventConsumer,  
															 EventCompleted::Outcome Status ) = 0;
	
	// The next event will then be dispatched by a method of the derived event 
	// queue manager. Note that all the now subscribers have been notified before
	// the event is dispatched, if the time has changed. 
	
	virtual void DispatchEvent( void ) = 0;
	
private:
	
  // ---------------------------------------------------------------------------
  // Receiving messages for completed events 
  // ---------------------------------------------------------------------------
	//
	// When an event has been properly processed by an actor it must return an
	// event completed message. This message is captured by the following handler
	// that asks the derived queue manager to delete the event, then it dispatches
	// the time of the next event to all Now subscribers, before it finally asks 
	// the derived queue manager to dispatch the next event.
	// 
	// The next event time will return the time of the event at the head of the 
	// event queue, which will be equal to current time until all events dispatched
	// for the current time have been acknowledged. 
	// The constructor starts the event handler actor and register this in the 
	// event handler pointer so that a second event hander will not be created. If
	// another event handler already exists a logic error exception is thrown.
	
	virtual void CompletedEventHandler( const EventCompleted & Ack, 
																			const Address EventProcessor )
	{
		CompletedEvent( EventProcessor, Ack.GetStatus() );
		UpdateNow();
	  DispatchEvent();
  }

	
public:
	
	// The constructor register the handler and the pointer to the instantiation 
	// of this class, or potentially throws the standard logic error if there 
	// is already an event handler registered.
	
	EventHandler( Framework & ExecutionFramework, 
								const std::string & HandlerName = std::string() )
	: Actor( ExecutionFramework,
					 HandlerName.empty() ? nullptr : HandlerName.data() ),
	  StandardFallbackHandler( ExecutionFramework, GetAddress().AsString() ),
	  EventClock< EventTime >( &CurrentTime ),
		NowSubscribers(), ConsistentTime( new TimeDistribution() )
	{
		if( EventHandlerName.empty() )
			EventHandlerName = GetAddress().AsString();
		else
			throw std::logic_error("There can only be one event handler at the time");
		
		RegisterHandler( this, &EventHandler::AddSubscriber 				);
		RegisterHandler( this, &EventHandler::RemoveSubscriber 			);
		RegisterHandler( this, &EventHandler::CompletedEventHandler );
	}

// The destructor clears the list of now subscribers and the event handler 
// Address. Note that this may result in some Now objects being stalled and 
// never again updated. The perfect solution would be to throw an exception if
// there was any subscriber left, but an exception is not allowed in a
// destructor in standard C++.
	
	virtual ~EventHandler()
	{
		EventHandlerName.clear();
		NowSubscribers.clear();
	}

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

template< class EventTime, class EventMessage >
class DiscreteEventManager : public virtual Actor,
														 public virtual StandardFallbackHandler,
														 public virtual EventData,
														 public virtual EventClock< EventTime >,
														 public virtual EventHandler< EventTime >
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
	public:
		const EventTime 	 TimeOfEvent;
		const Address	  	 EventReceiver;
		const EventMessage TheMessage;
		
		Event( const EventTime & TheTime, const EventMessage & MessageToSend,
					 const Address TheReceiver = Address::Null() )
		: TimeOfEvent( TheTime ), EventReceiver( TheReceiver ), 
		  TheMessage( MessageToSend )
		{ }
		
		Event( const Event & OtherEvent )
		: TimeOfEvent( OtherEvent.TimeOfEvent ), 
		  EventReceiver( OtherEvent.EventReceiver ),
		  TheMessage( OtherEvent.TheMessage )
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
  
  // Derived classes will need to know if the queue is empty 

  inline bool PendingEvents ( void )
  {
    return !EventQueue.empty();
  }
  
  // The event is enqueued by a separate function. The reason for this is 
  // explained in relation to the enqueue message handler below.
  
  inline void QueueEvent( const EventTime & TimeOfEvent, 
													const Address & TheEventReceiver, 
													const EventMessage & TheMessage )
	{
	  EventQueue.emplace( TimeOfEvent, 
												EnqueuedEvent( TheEventReceiver, TheMessage ) );			
	}
  
  // One could think that that retuning the time of the next event would be 
  // just reading it out of the queue. However, in the situation where the 
  // last event has been handled, then the queue may be empty and there is no 
  // first event time to return. In this case there is no first time to return,
  // and only the current time can be returned (based on the Current Time tick
  // counter). A special syntax has to be used to make the compiler understand 
  // that the template Now function of the base class is called and not the 
  // Now receiver object. 
  
  virtual EventTime NextEventTime ( void )
  {
		if ( ! EventQueue.empty() )
	    return EventQueue.begin()->first;
		else
			return EventClock< EventTime>::template Now< EventTime >();
  }
  
  // ---------------------------------------------------------------------------
  // Event dispatch
  // ---------------------------------------------------------------------------
  //    
  // Time in a discrete event simulation advances from time tick to 
  // time tick, where the time ticks are the points in time where one or
  // more of the actors will have something to do. By definition, everything
  // happening at the same time tick will happen in parallel. Still,  if there
  // are dependencies among events, this could easily create unforeseen 
  // conflicts, and the safest is to dispatch one and only one event.
  // 
  // It should be noted that the function is called from the event 
  // acknowledgement handler. It is therefore no need to test the time or 
  // to update the time as this has already been taken care of by the Event 
  // Handler. However the Event hander will not know if the last event in the 
  // queue was handled, and therefore it is necessary to test for this, and 
  // since the most efficient way is to have a pointer (iterator) to the first
  // event in the queue when looking up the message and the address, the test
  // is done based on this iterator instead of testing directly if the queue is 
  // "empty()"
  
  virtual void DispatchEvent ( void )
  {
		  auto CurrentEvent = EventQueue.begin();
		  
			if ( CurrentEvent != EventQueue.end()  )
				Send( CurrentEvent->second.Message, 
							CurrentEvent->second.EventReceiver );
  };

  // ---------------------------------------------------------------------------
  // Setting up events
  // ---------------------------------------------------------------------------
  //    
  // An actor enqueues an event by sending the event message to this manager.
	// If the event receiver given by the event class is null it is taken to be 
	// the sender of the event request.
	//
  // The concept of time is not easy since the approaches are very different 
	// depending on whether the event queue is used as part of a simulation or 
	// if the events happens at future real time epochs. In the former case, the 
	// the system time is defined by the event time, and it is impossible to 
	// enqueue an event with a time earlier than the current time. However, in 
	// a situation where the true system clock gives the time, the first event 
	// in the queue is at some future time epoch, and it is perfectly possible to 
	// enqueue a new event between this epoch and the current system clock time. 
	//
	// The default behaviour of the enqueue event message handler is to implement 
	// the simulated time policy and check that the given time is larger than 
	// the current, and simply drop events that would be in the past. It is 
	// possible to overload this behaviour in a derived class.
    
private:
	
  virtual void EnqueueEvent ( const Event & TheEvent, 
											        const Theron::Address RequestingActor )
  {
		if ( CurrentTime <= ToTimeCounter( TheEvent.TimeOfEvent ) )
		{
			Address EventReceiver;
			
			if ( TheEvent.EventReceiver == Address::Null() )
				EventReceiver = RequestingActor;
			else
				EventReceiver = TheEvent.EventReceiver;
			
			QueueEvent( TheEvent.TimeOfEvent, EventReceiver, TheEvent.TheMessage );
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
	//
	// A minor implementation detail: The event time can have any format, but 
	// the current time counter is always a counter. The counter can be converted
	// to the time of the first event, however it would be better simply to read
	// out the time of the first event. This first event will always exist, 
	// because the complete event function is supposed to remove an event, and
	// therefore there must be at least one event in the queue. The current time
	// is then just the first element (key) of the beginning of the queue.
	
protected:
	
	virtual void CompletedEvent( Address EventConsumer,  
															 EventCompleted::Outcome Status )
	{
		std::pair< EventReference, EventReference > 
			EventRange = EventQueue.equal_range( EventQueue.begin()->first );
			
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
		{ }
		
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
    : Actor( TheFramework, name.empty() ? nullptr : name.data() ),
      StandardFallbackHandler( TheFramework, GetAddress().AsString() ),
      EventData(), EventClock< EventTime >( &CurrentTime ),
      EventHandler< EventTime >( TheFramework, GetAddress().AsString() ),
      EventQueue()
  {
		RegisterHandler( this, 
		&DiscreteEventManager< EventTime, EventMessage >::EnqueueEvent );
  };
    
  // The destructor simply clears whatever unhanded events there might
  // be left in the queue, and forgets the time subscribers. It must be 
  // virtual since the handler has virtual functions, and other classes 
  // can derive from it.
  
  virtual ~DiscreteEventManager()
  {
    EventQueue.clear();
  };
};			// End Discrete Event Handler

}       // End name space Theron
#endif  // EVENT_HANDLER
