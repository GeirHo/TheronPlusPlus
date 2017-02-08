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

#include <Theron/Theron.h>
#include <set>
#include <stdexcept>
#include "EventHandler.hpp"

namespace Theron
{

// ---------------------------------------------------------------------------
// Static variables 
// ---------------------------------------------------------------------------

// A flag to ensure that there is one and only one event handler in the system
	
EventHandler * EventHandler::TheEventHandler = nullptr;

// A set of actors that should be notified when the event time moves to the 
// next event in the queue.

std::set< Address > EventHandler::NowSubscribers;

// The current time is initialised to zero since time is POSIX time which is 
// a positive number of some resolution. All event times must be larger or 
// equal to this time. 

EventHandler::EventTime EventHandler::CurrentTime(0);

// ---------------------------------------------------------------------------
// Event Handler 
// ---------------------------------------------------------------------------

// When an event has been properly processed by an actor it must return an
// event completed message. This message is captured by the following handler
// that asks the derived queue manager to delete the event, then it dispatches
// the time of the next event to all Now subscribers, before it finally asks 
// the derived queue manager to dispatch the next event.
// 
// It should be noted that all the events at the same time can be dispatched 
// at the same time. In other words, this implies that the clock should only 
// be updated if the next event is into the future, and then the events at 
// that time should be dispatched. 
//
// The next event time will return the time of the event at the head of the 
// event queue, which will be equal to current time until all events dispatched
// for the current time have been acknowledged. 

void EventHandler::CompletedEventHandler( 
									 const EventHandler::EventCompleted & Ack, 
									 const Address EventProcessor )
{
	CompletedEvent( EventProcessor, Ack.Status );
	
	EventTime NextTime = NextEventTime();
	
	if ( CurrentTime < NextTime )
	{
		for ( Address NowReceiver : NowSubscribers )
			Send( CurrentTime, NowReceiver );

		DispatchEvents();
	}
}

// The constructor starts the event handler actor and register this in the 
// event handler pointer so that a second event hander will not be created. If
// another event handler already exists a logic error exception is thrown.

EventHandler::EventHandler( Framework & ExecutionFramework, 
														const std::string & HandlerName )
: Actor( ExecutionFramework,
				 HandlerName.empty() ? nullptr : HandlerName.data() )
{
	if( TheEventHandler == nullptr )
		TheEventHandler = this;
	else
		throw std::logic_error( "It can only be one event handler at the time" );
	
	RegisterHandler( this, &EventHandler::CompletedEventHandler );
}

// The destructor clears the list of now subscribers and the event handler 
// pointer. Note that this may result in some Now objects being stalled and 
// never again updated. The perfect solution would be to throw an exception if
// there was any subscriber left, but an exception is not allowed in a
// destructor in standard C++.

EventHandler::~EventHandler( void )
{
	NowSubscribers.clear();
	TheEventHandler = nullptr;
}

} 	// End of name space Theron
