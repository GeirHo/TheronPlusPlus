/*==============================================================================
AMQ Endpoint

This file defines the endpoint functionality. In particular the constructor
of the AMQ Network base class.

Author and Copyright: Geir Horn, University of Oslo, 2018-2019
License: LGPL 3.0
==============================================================================*/

#include <string>
#include <sstream>
#include <stdexcept>

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

// The function to get the address of a particular layer server will return
// this provided that the layer server exists. Otherwise, it will throw an
// logic error exception.

Theron::Actor::Address
Theron::ActiveMQ::Network::GetAddress( Theron::Network::Layer Role )
{
	Address     LayerServer;
	std::string ServerTypeInError;

  switch( Role )
  {
		case Theron::Network::Layer::Network:
			if ( AMQNetwork != nullptr )
	      LayerServer = AMQNetwork->NetworkLayerAddress();
			else
				ServerTypeInError = "Network Layer";
      break;
		case Theron::Network::Layer::Session:
			if ( AMQNetwork != nullptr )
	      LayerServer = AMQNetwork->SessionLayerAddress();
			else
				ServerTypeInError = "Session Layer";
      break;
		case Theron::Network::Layer::Presentation:
			if ( AMQNetwork != nullptr )
	      LayerServer = AMQNetwork->PresentationLayerAddress();
			else
				ServerTypeInError = "Presentation Layer";
     break;
  }

  if ( ServerTypeInError.empty() )
    return LayerServer;
	else
	{
		std::ostringstream ErrorMessage;

		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
		             << "The address of the " << ServerTypeInError
		             << " server is requested before the network endpoint has been"
								 << " initialised";

	  throw std::logic_error( ErrorMessage.str() );
	}
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
