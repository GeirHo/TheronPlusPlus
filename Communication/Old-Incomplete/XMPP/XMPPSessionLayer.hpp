/*=============================================================================
  Session Layer

  The standard session layer takes care of managing the external addresses
  of the local actors, and map message addresses to internal actors.

  Author and copyright: Geir Horn, University of Oslo, 2015 - 2020
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef THERON_XMPP_SESSION_LAYER
#define THERON_XMPP_SESSION_LAYER

// Headers for the Theron++ actor system

#include "Actor.hpp"
#include "Utility/StandardFallbackHandler.hpp"
#include "Communication/SessionLayer.hpp"
#include "Communication/XMPP/XMPPMessages.hpp"

namespace Theron::XMPP
{
/*==============================================================================

 Session Layer

==============================================================================*/
//
// The XMPP Session layer has no additional functionality extending what is
// already covered by the generic session layer. However, it must define the
// functions to give the addresses of the network layer and the presentation
// layer of this protocol so that the generic Session Layer sends the packets
// to the right destinations.

class SessionLayer
: virtual public Actor,
  virtual public StandardFallbackHandler,
	virtual public Theron::SessionLayer< ExternalMessage >
{
protected:

	// ---------------------------------------------------------------------------
	// Server address management
	// ---------------------------------------------------------------------------
	//
  // The session layer communicates with the network layer and the presentation
	// layer and needs the addresses for these servers, which can be obtained
	// from the endpoint.

	virtual Address NetworkLayerAddress     ( void ) const final;
	virtual Address PresentationLayerAddress( void ) const final;

	// ---------------------------------------------------------------------------
	// Constructor and destructor
	// ---------------------------------------------------------------------------
	//
  // The only thing that is needed to create the XMPP session layer is the
  // actor name, although it does have a default name for this endpoint as
  // there should be only one XMPP session layer per endpoint.

public:

	SessionLayer( const std::string & ServerName = "XMPPSessionLayer" )
	: Actor( ServerName ),
	  StandardFallbackHandler( Actor::GetAddress().AsString() ),
	  Theron::SessionLayer< ExternalMessage >( Actor::GetAddress().AsString() )
	{}

	virtual ~SessionLayer()
	{}
};

}      // End of name space Theron XMPP
#endif // THERON_XMPP_SESSION_LAYER
