/*=============================================================================
  XMPP Messages

  This file implements the actor address function of the external message
  class since this is virtual. It also implements the presentation layer
  address function of the serial message as this is both virtual and dependent
  on the static functions of the network endpoint.

  Author and copyright: Geir Horn, University of Oslo, 2015 - 2020
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include "Communication/XMPP/XMPPMessages.hpp"
#include "Communication/XMPP/XMPPEndpoint.hpp"

/*==============================================================================

 External messages

==============================================================================*/

Theron::Address Theron::XMPP::ExternalMessage::ActorAddress(
	const Theron::XMPP::JabberID & ExternalActor ) const
{
	std::string TheAddress( ExternalActor.getResource() );

	if ( TheAddress.empty() )
		return Address( ExternalActor.toString() );
	else
		return Address( TheAddress );
}

/*==============================================================================

 Serial messages

==============================================================================*/

Theron::Address Theron::XMPP::SerialMessage::PresentationLayerAddress() const
{
	return Network::GetAddress( Network::Layer::Presentation );
}
