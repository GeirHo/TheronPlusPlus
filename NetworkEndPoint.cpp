/*=============================================================================
  Network End Point

  Author: Geir Horn, University of Oslo, 2015-2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include <thread>			// To wait for all messages to be served
#include <chrono>			// To wait one second when there are unhanded messages
#include <algorithm>	// To accumulate unhanded messages
#include <sstream>    // Formatted error messages
#include <stdexcept>  // To throw exceptions if no shut down receiver

#include "NetworkEndPoint.hpp"

/*=============================================================================
 
  Static variables

=============================================================================*/

// Static parameter class for the Theron Framework

Theron::NetworkEndPoint::FrameworkParameters 
								 Theron::NetworkEndPoint::Parameters;
								 
// The pointers to the OSI stack layer servers are kept in a static map in order
// for other actors to get the relevant addresses to register and de-register 
// with these servers if necessary.
								 
std::map< Theron::NetworkEndPoint::Layer , std::shared_ptr< Theron::Actor > > 
				  Theron::NetworkEndPoint::CommunicationActor;

/*=============================================================================

  Shut down management

=============================================================================*/

// The handler for the termination message. It will check if any of the actors 
// guarded has any messages, and if that is the case it will suspend the thread
// serving the message for one second. It would be better to avoid this polling,
// but there is no other event to monitor.

void Theron::NetworkEndPoint::TerminationReceiver::StartTermination(
		 const Theron::NetworkEndPoint::ShutDownMessage & TheRequest, 
		 const Theron::Address Sender)
{
	while ( std::any_of( GuardedActors.begin(), GuardedActors.end(), 
											 [](const Theron::Actor * TheActor)->bool{ 
											 	  return TheActor->GetNumQueuedMessages() > 0;  }) )
		std::this_thread::sleep_for( std::chrono::seconds(1) );
}

// The message to send the shut down request takes the address of a sending 
// actor and sends the shut down message to the receiver if the receiver has 
// been created. Otherwise, it throws a standard logic error. 

void Theron::NetworkEndPoint::SendShutDownMessage(
																								const Theron::Address & Sender)
{
	if ( ShutDownReceiver )
		Send( ShutDownMessage(), Sender, ShutDownReceiver->GetAddress() );
	else
	{
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << "Sending a shut down message requires a shut down " 
								 << "receiver";
		
		throw std::logic_error( ErrorMessage.str() );
	}
		
}

// To support the cases where the user would want to send the shut down 
// message from another actor directly without using the shut down message 
// function, the address of the shut down receiver can be obtained provided 
// that the receiver has been allocated. Otherwise, a logic error exception is 
// thrown.

Theron::Address Theron::NetworkEndPoint::ShutDownAddress( void )
{
	if ( ShutDownReceiver )
		return ShutDownReceiver->GetAddress();
	else
	{
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << "No shut down receiver to give the address of";
								 
		throw std::logic_error( ErrorMessage.str() );
	}
		
}

