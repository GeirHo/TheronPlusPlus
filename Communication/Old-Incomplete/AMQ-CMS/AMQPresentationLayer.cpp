/*=============================================================================
  Presentation Layer

  A special presentation layer for the AMQ transport is only needed to
  implement the virtual address function for the session layer address.

  Author: Geir Horn, University of Oslo, 2015 - 2019
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include "Communication/AMQ/AMQPresentationLayer.hpp"
#include "Communication/AMQ/AMQEndpoint.hpp"

Theron::Actor::Address
Theron::ActiveMQ::PresentationLayer::SessionLayerAddress( void ) const
{
	return
	Theron::ActiveMQ::Network::GetAddress( Theron::Network::Layer::Session );
}

