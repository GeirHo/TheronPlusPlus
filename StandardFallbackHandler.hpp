/*=============================================================================
Standard Fall back Handler

Theron allows an actor to define a default message handler that will be called
if there is no registered message handler for the incoming message type. The
handler will receive a pointer to the message data, the size of the data buffer,
and the address of the sender of the message. 

An actor can do little to a message of unknown type, except to print the content
of the data buffer. A default handler may also serve as a good location for 
a breakpoint in debugging. 

This implies, however, that the fall back handler will be identical independent 
of the actor type, and it is bad practice to duplicate the same code for all 
actors used in a project. Hence, this simple class defines a minimal virtual 
actor and register a fall back handler for this actor. 

The actor base class is declared as virtual, meaning that if this object is 
inherited by a real actor after the Theron::Actor base class, it will share the 
same actor base class object and act as a fall back handler for the actor 
without the need to duplicate code.

Furthermore, this standard fall back handler will throw an exception, which may
be caught if it is possible to recover from this runtime error.

Author and Copyright: Geir Horn, 2017
License: LGPL 3.0
=============================================================================*/

#ifndef STANDARD_FALLBACK_HANDLER
#define STANDARD_FALLBACK_HANDLER

#include <sstream>					// To format the error message
#include <stdexcept>				// To throw a standard exception
#include <string>					  // To name the actor

#include <iostream>
#include <Theron/Theron.h>	// Theron Actor framework

namespace Theron
{

class StandardFallbackHandler : public virtual Actor
{
private:
	
	// The private fall back handler format a string based on the information 
	// provided by Theron, and then throw an exception with this string as 
	// explanation.
	
	void FallbackHandler( const void *const Data, 
												const uint32_t Size, 
												const Address From )
	{
		std::ostringstream ErrorMessage;
		const char * ByteString = reinterpret_cast< const char * >( Data );
		
		ErrorMessage << "Unhandled message "
								 << " to " << GetAddress().AsString()
								 << " from " << From.AsString() << " with data ";
								 
		for ( unsigned int i=0; i < Size; i++ )
			ErrorMessage << ByteString[i] << " ";
		
		//throw std::runtime_error( ErrorMessage.str() );
		std::cout << " *** ERROR  *** " << ErrorMessage.str() << std::endl;
	}
	
public:
	
	// The public constructor takes a reference to the framework and register
	// the standard handler for this actor. It takes the actor name as a string,
	// which is default empty, for which it leaves the assignment of the actor 
	// name to the Theron framework.
	//
	// The same handler is registered also as the actor's fall back handler
	
	StandardFallbackHandler( Framework & TheFramework, 
													 const std::string & ActorName = std::string() )
	: Actor( TheFramework, ActorName.empty() ? nullptr : ActorName.data() )
	{
		SetDefaultHandler( this, &StandardFallbackHandler::FallbackHandler );
		
		TheFramework.SetFallbackHandler( this, 
																		 &StandardFallbackHandler::FallbackHandler);
	}
	
}; 	// End class standard fall back handler
} 	// End name space Theron
#endif
