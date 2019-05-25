/*=============================================================================
Termination watch

A small problem in actor systems is that there is no function to wait for
termination. Each actor is in principle a state machine that will run
forever exchanging messages. The thread that starts main and implicitly creates
all the actors should be suspended until the actor system
itself decides to terminate. The correct way to do this is to use a
Receiver object that will get a last message when all the key actors in
the system no longer have messages to process. Thus when this receiver
terminates its wait, the main() will probably terminate calling the
destroying the objects created within main().

In larger systems there will be many actors. The result is that some threads
may be waiting for an available core. Thus, when the deciding actor has
entered the finishing state, there may still be unhanded messages for
other actors, and some of these may be processed in other threads while
main() is destroying the actors almost guaranteeing that the application
will terminate with a segmentation fault.

It could be tempting to 'join' the message producing threads of the actors in
the system. However the threads will just be blocked, never stopped before the
actor is told to terminate. Hence the only possibility is to wait for all
actors to have stopped processing messages. Given that the Theron++ scheduler
is the operating system, it is not possible to know when there are no pending
messages. However, if the actors created in main() are known, then it is
possible to verify that all their message queues are empty before terminating.

This idea is implemented with a Receiver that has a set of pointers to
actors to wait for. When it receives the shut down message it will loop
over the list of actors checking if they have pending messages, and if so
it will suspend execution for a user defined time, by default one second.
The handler will only terminate when there are no pending messages for any of
the guarded actors.

The Termination Watch class should only be used for subset of actors. If one
wants to await in main for ALL actors in a system it is recommended to use the
Actor::WaitForTermination() function instead as the Termination Watch class
needs the pointers for the actor objects to wait for.

Author and Copyright: Geir Horn, University of Oslo, 2018-2019
License: LGPL 3.0
==============================================================================*/

#ifndef THERON_TERMINATION_WATCH
#define THERON_TERMINATION_WATCH

#include <set>          // To hold the watched actors
#include <chrono>       // The concept of time
#include <thread>       // To pause the receiver's message thread
#include <algorithm>    // The any_of function
#include "Actor.hpp"    // The Theron++ actor framework

namespace Theron
{

class TerminationWatch : virtual public Receiver
{
private:

	std::set< const Actor * > GuardedActors;

	// There is a shut down message which is just a flag that indicates that
	// the sender expects that the actor system should have finished.

public:

	class ShutDown
	{
	public:

		ShutDown( void ) = default;
		ShutDown( const ShutDown & Other ) = default;
	};

	// The receiver class has a wait() that is used by a thread having access to
	// the receiver object to wait for the complete handling of a message. Thus,
	// when the handler blocks the receiver's message handling thread, it will
	// also block the thread waiting for the receiver.

private:

	void StartTermination( const ShutDown & TheRequest,
												 const Address Sender )
	{
		while ( std::any_of( GuardedActors.begin(), GuardedActors.end(),
									 [](const Theron::Actor * TheActor)->bool{
										  return TheActor->GetNumQueuedMessages() > 0;  }) )
			std::this_thread::sleep_for( std::chrono::seconds(1) );
	}

public:

	// The constructor takes an initialisation list to ensure that all pointers
	// are for actors.

	TerminationWatch(
								const std::initializer_list< const Actor * > & ActorsToGuard )
	: GuardedActors( ActorsToGuard )
	{
		RegisterHandler( this, &TerminationWatch::StartTermination );
	}

	// There is a nicer alias that takes the list of actor objects to monitor
	// and simply delegates to the above constructor to do the actual
	// initialization.

	template< class... ActorTypes >
	TerminationWatch( ActorTypes & ...TheActor )
	: TerminationWatch({ dynamic_cast< const Actor * >( &TheActor )... })
	{}

};

}      // Name space Theron++
#endif //THERON_TERMINATION_WATCH
