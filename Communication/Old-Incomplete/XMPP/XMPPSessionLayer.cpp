/*=============================================================================
  Session Layer

  The implementation of the session layer is just for defining the two virtual
  address functions derived from the endpoint's static function.

  Author and copyright: Geir Horn, University of Oslo, 2015 - 2020
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include "Communication/XMPP/XMPPSessionLayer.hpp"
#include "Communication/XMPP/XMPPEndpoint.hpp"

/*==============================================================================

 Network layer server addresses

==============================================================================*/

Theron::Actor::Address
Theron::XMPP::SessionLayer::NetworkLayerAddress() const
{
	return Network::GetAddress( Network::Layer::Network );
}

Theron::Actor::Address
Theron::XMPP::SessionLayer::PresentationLayerAddress() const
{
	return Network::GetAddress( Network::Layer::Presentation );
}




