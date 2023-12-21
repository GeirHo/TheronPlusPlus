/*==============================================================================
ZMQ Session layer

The main task of the session layer is to keep track of the external addresses
of actors to allow messages to be sent to actors without knowing where the
actor is. Local actors with an external presence must register with the
session layer when starting, and de-register when closing. Local actors
will be assigned the TCP endpoint of this node.

The standard session layer provides most of the functionality needed.
However, it serves the local actor with external addresses, and with
addresses of remote actors they want to exchange messages with. There is
no support for responding to the question that may be raised by remote
session layers: Is a given actor hosted on this node?

This functionality cannot be supported generically since it is not needed by
all network layer protocols. Some protocols simply broadcast new actors to
all endpoints and then there is no need to resolve where a particular actor
is hosted.

The ZMQ network layer will send a local actor inquiry message to the session
layer. The session layer handler for this message will fill
the address if the actor exists on this endpoint and return the response
to the link. If the actor does not exist, the inquiry will just be forgotten.

Author and Copyright: Geir Horn, University of Oslo, 2017-2019
License: LGPL 3.0
==============================================================================*/

#ifndef THERON_ZMQ_SESSION_LAYER
#define THERON_ZMQ_SESSION_LAYER

#include "Actor.hpp"
#include "Utility/StandardFallbackHandler.hpp"
#include "Communication/SessionLayer.hpp"

#include "Communication/ZMQ/ZMQMessages.hpp"
#include "Communication/ZMQ/ZMQNetworkLayer.hpp"

namespace Theron::ZeroMQ
{
class SessionLayer : virtual public Actor,
										 virtual public StandardFallbackHandler,
										 public Theron::SessionLayer< OutsideMessage >
{
private:

	void CheckActor( const NetworkLayer::LocalActorInquiry & TheRequest,
									 const Address TheLinkServer );

public:

	SessionLayer( const std::string & ServerName = "SessionLayer" )
	: Actor( ServerName ), StandardFallbackHandler( GetAddress().AsString() ),
	  Theron::SessionLayer< OutsideMessage >( GetAddress().AsString() )
	{
		RegisterHandler( this, &SessionLayer::CheckActor );
	}
};

}      // Name space Theron::ZeroMQ
#endif // THERON_ZMQ_SESSION_LAYER
