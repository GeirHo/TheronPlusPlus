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

#include <set>
#include <stdexcept>
#include <sstream>

#include "Actor.hpp"
#include "EventHandler.hpp"

namespace Theron
{

// ---------------------------------------------------------------------------
// Static variables 
// ---------------------------------------------------------------------------

// A flag to ensure that there is one and only one event handler in the system
	
std::string EventData::EventHandlerName;

// ---------------------------------------------------------------------------
// Now management 
// ---------------------------------------------------------------------------
//
// The representation of the current time is internally set to the nanoseconds
// since the start of the epoch of the clock. For instance in POSIX time it 
// would be nanoseconds since 1970. 

EventData::TimeCounter EventData::CurrentTime;


} 	// End of name space Theron
