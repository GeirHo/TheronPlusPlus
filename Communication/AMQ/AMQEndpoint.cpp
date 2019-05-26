/*==============================================================================
AMQ Endpoint

This file defines the endpoint functionality. In particular the constructor
of the AMQ Network base class.

Author and Copyright: Geir Horn, University of Oslo, 2018-2019
License: LGPL 3.0
==============================================================================*/

#include "AMQMessages.hpp"
#include "AMQEndpoint.hpp"
#include "AMQNetworkLayer.hpp"
#include "AMQSessionLayer.hpp"
#include "AMQPresentationLayer.hpp"

// The static pointer to the network layer needed to access the addresses of
// the various servers by a static function on the network class.

Theron::ActiveMQ::Network * Theron::ActiveMQ::Network::AMQNetwork = nullptr;

// The constructor initialises all the layers with the different configuration
// parameters

Theron::ActiveMQ::Network::Network(
	const std::string & EndpointName,
	const std::string & AMQServerIP, const std::string & AMQServerPort,
	const std::string & SessionServer, const std::string&  PresentationServer,
	const std::string & AMQPrefix)
: Actor( EndpointName ),
  StandardFallbackHandler( Actor::GetAddress().AsString() ),
  Theron::Network( Actor::GetAddress().AsString() ),
  NetworkServer( AMQPrefix + EndpointName, AMQServerIP, AMQServerPort ),
  SessionServer( AMQPrefix + SessionServer ),
  PresentationServer( AMQPrefix + PresentationServer )
{
	AMQNetwork = this;
}

/*==============================================================================

 Serial messages

==============================================================================*/
//
// It needs to implement the virtual lookup function returning the AMQ
// Presentation Layer server address.

Theron::Address
Theron::ActiveMQ::SerialMessage::PresentationLayerAddress( void ) const
{
	return ActiveMQ::Network::GetAddress( Theron::Network::Layer::Presentation );
}
