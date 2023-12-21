/*=============================================================================
  Presentation Layer

  The only function that needs to be implemented for the presentation layer
  is the session layer address function that can be obtained from the network
  endpoint, but needed to be implemented in a separate file to avoid circular
  dependencies on the definitions.

  Author and copyright: Geir Horn, University of Oslo, 2015 - 2020
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include "Communication/XMPP/XMPPPresentationLayer.hpp"
#include "Communication/XMPP/XMPPEndpoint.hpp"

Theron::Address
Theron::XMPP::PresentationLayer::SessionLayerAddress( void ) const
{
	return Network::GetAddress( Network::Layer::Session );
}
