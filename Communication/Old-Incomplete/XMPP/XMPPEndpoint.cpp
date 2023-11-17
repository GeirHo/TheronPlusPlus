/*=============================================================================
  XMPP Endpoint

  The static function looking up the addresses of the various layers must be
  implemented based on the static pointer to the endpoint that will be set
  when the endpoint actor is created.

  Author and copyright: Geir Horn, University of Oslo, 2015 - 2020
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include <string>
#include <sstream>
#include <stdexcept>

#include "Communication/XMPP/XMPPEndpoint.hpp"

/*==============================================================================

 Server address management

==============================================================================*/
//
// There is a static pointer to the XMPP network which enables the static
// address function to obtain the addresses of the various network layer
// servers.

Theron::XMPP::Network * Theron::XMPP::Network::XMPPNetwork = nullptr;

// This pointer is tested before being accessed in order to capture a possible
// null-pointer access and ensure that the error message is readable.

Theron::Actor::Address
Theron::XMPP::Network::GetAddress( Theron::Network::Layer Role )
{
	Address     LayerServer;
	std::string ServerTypeInError;

  switch( Role )
  {
		case Theron::Network::Layer::Network:
			if ( XMPPNetwork != nullptr )
	      LayerServer = XMPPNetwork->NetworkLayerAddress();
			else
				ServerTypeInError = "Network Layer";
      break;
		case Theron::Network::Layer::Session:
			if ( XMPPNetwork != nullptr )
	      LayerServer = XMPPNetwork->SessionLayerAddress();
			else
				ServerTypeInError = "Session Layer";
      break;
		case Theron::Network::Layer::Presentation:
			if ( XMPPNetwork != nullptr )
	      LayerServer = XMPPNetwork->PresentationLayerAddress();
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

 Constructor and destructor

==============================================================================*/
//
// The constructor initializes the subclasses and sets the static pointer to
// this class.

Theron::XMPP::Network::Network(
	const std::string & EndpointName, const std::string & Domain,
	const std::string & ServerPassword,
	const Theron::XMPP::JabberID & InitialRemoteEndpoint,
	const std::string & SessionServerName,
	const std::string & PresentationServerName,
	const std::string & Prefix )
: Actor( EndpointName ),
  StandardFallbackHandler( Actor::GetAddress().AsString() ),
  Theron::Network( Actor::GetAddress().AsString() ),
  NetworkServer( EndpointName, Domain, ServerPassword, InitialRemoteEndpoint,
								 Prefix + EndpointName	),
  SessionServer( Prefix + SessionServerName ),
  PresentationServer( Prefix + PresentationServerName )
{
	XMPPNetwork = this;
}

// The destructor just resets the global static pointer to this class that
// no longer exists so that attempts on using the static address lookup will
// fail.

Theron::XMPP::Network::~Network()
{
	XMPPNetwork = nullptr;
}
