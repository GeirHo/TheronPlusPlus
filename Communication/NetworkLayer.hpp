/*=============================================================================
  Network Layer

  The purpose of the network layer is to manage the interface to the outside. It
  will most likely build on some other library managing low level stuff like
  sockets and IP protocols. An implementation will probably also run a thread
  which will be reacting to incoming packets.

  The provided code is therefore mainly a stub defining some hooks for derived
  classes to implement the necessary protocol level functionality without
  having to understand Theron, or the way the hierarchy of communication actors
  interact in order to serve the actors.

  It could even be that this implements some low level protocol. One typical
  example is that the protocol engine assumes that each externally communicating
  actor has a unique external address. However, if the link is implemented as
  one single socket on one single IP, all the actors share the same IP. In this
  case the unique protocol address could be to send a message to
  "ActorID@192.168.10.12:445" which must be understood as by the link server as
  IP=192.168.10.12, Port=445 and then the ActorID must be added to the datagram.
  In other words, it can well be also a link level protocol that must be
  implemented by this actor before sending the message, or before delivering a
  received message to the protocol engine.

  Author: Geir Horn, University of Oslo, 2015-2019
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef THERON_NETWORK_LAYER
#define THERON_NETWORK_LAYER

#include <string>
#include <type_traits>
#include <stdexcept>
#include <ostream>
#include <concepts>

#include "Actor.hpp"
#include "Utility/StandardFallbackHandler.hpp"

#include "Communication/NetworkEndpoint.hpp"
#include "Communication/LinkMessage.hpp"

namespace Theron
{
/*==============================================================================

 Network layer

==============================================================================*/
//
// The network layer depends on the external message format to be exchanged
// with remote network endpoints.

template< ValidLinkMessage ExternalMessage >
class NetworkLayer : virtual public Actor,
										 virtual public StandardFallbackHandler
{
public:

  using MessageType = ExternalMessage;

  // For the link server it is not essential that the external message confirms
  // to the Link Message interface, but it is a requirement for the protocol
  // engine, hence the condition is enforced also here.

  static_assert( std::is_base_of<
				  LinkMessage< typename ExternalMessage::AddressType, 
											 typename ExternalMessage::PayloadType  >,
											 ExternalMessage >::value,
				  "Network Layer: External message must be derived from Link Message" );

 	// ---------------------------------------------------------------------------
	// Resolving addresses
	// ---------------------------------------------------------------------------
	//
  // When an actor address needs to be found in the distributed actor system
  // The address resolution and assignment of the external address is dependent
  // on the actual communication technology being used. Hence, the session
  // layer will send a resolution request for a given remote actor address.
	// The message also contains the requesting actor so that the remote actor 
	// can set up a 'subscription' for this the local actor.

  class ResolutionRequest
  {
	public:

		const Address RequestedActor;
		const typename ExternalMessage::AddressType RequestingActor;

		ResolutionRequest( const Address & TheRemoteActor, 
											 const typename ExternalMessage::AddressType & LocalActor)
		: RequestedActor( TheRemoteActor ), RequestingActor( LocalActor )
		{ }

		ResolutionRequest( const ResolutionRequest & Other )
		: RequestedActor(  Other.RequestedActor, Other.RequestingActor ) 
		{ }

		ResolutionRequest( void ) = delete;
	};

	// The handler for this message is left to the concrete protocol to
	// implement, but it should send back the external address for the given
	// actor. It could be that the external address encodes the actor address,
	// but this is technology dependent, and a generic response is returned
	// for which the actor's address is explicitly recorded.

	class ResolutionResponse
	{
	public:

		const typename ExternalMessage::AddressType 
		RequestedActor, RequestingActor;

		inline ResolutionResponse(
					const typename ExternalMessage::AddressType & ResolvedAddress,
					const typename ExternalMessage::AddressType & ActorRequesting )
		: RequestedActor( ResolvedAddress ), RequestingActor( ActorRequesting )
		{ }

		inline ResolutionResponse( const ResolutionResponse & Other )
		: ResolutionResponse( Other.RequestingActor, Other.RespondingActor )
		{ }

		ResolutionResponse( void ) = delete;
	};

	// The handler for the resolution request cannot be implemented because it
	// depends on the technology used for the link, but it should take a
	// resolution request and return a resolution response to the session layer.

protected:

	virtual void ResolveAddress( const ResolutionRequest & TheRequest,
														   const Address TheSessionLayer ) = 0;

  // It is the task of the session layer to check if a resolution request
  // arriving on the network is a local (de-serializing) actor. Hence when
  // such a request arrives, the network layer must forward this to the session
  // layer and the session layer responds with a resolution response if the
  // this is a local actor. There is a handler for this response that should
  // send the resolution response back to remote endpoints.

  virtual void ResolvedAddress( const ResolutionResponse & TheResponse,
																const Address TheSessionLayer ) = 0;

  // There is also a mechanism to remove a local actor that is previously
  // registered. The external address should in this case be known, and
  // only this is therefore transmitted.

public:

  class RemoveActor
  {
	public:

		const typename ExternalMessage::AddressType GlobalAddress;

		RemoveActor( const typename ExternalMessage::AddressType & GlobalReference )
		: GlobalAddress( GlobalReference )
		{ }

		RemoveActor( void ) = delete;
		RemoveActor( const RemoveActor & Other )
		: GlobalAddress( Other.GlobalAddress )
		{ }
	};

protected:

	virtual void ActorRemoval( const RemoveActor & TheCommand,
														 const Address TheSessionLayer ) = 0;

	// ---------------------------------------------------------------------------
	// Input and output messages
	// ---------------------------------------------------------------------------
	//
  // There is a handler to receive the messages from the session server
  // It implements the specific actions of the link layer protocol necessary to
  // send the message, and it is the Theron message handler called when an
  // external message is received from the session layer. It must be
  // implemented by derived protocol specific classes.

  virtual void OutboundMessage( const ExternalMessage & TheMessage,
																const Address From ) = 0;

	// ---------------------------------------------------------------------------
  // Shut-down management
  // ---------------------------------------------------------------------------
	//
	// When the shut down message arrives, all local actors shall have asked to
	// be removed, and the network layer must just inform its peers that this
	// endpoint is shutting down and then close the connection. These actions
	// are technology dependent and the handler for the shut down message cannot
	// be provided at this stage.

	virtual void Stop( const Network::ShutDown & StopMessage,
										 const Address Sender ) = 0;

	// ---------------------------------------------------------------------------
	// Constructor and destructor
	// ---------------------------------------------------------------------------
  //
  // The constructor initialises the actor making sure that it has the right
  // name, and registers the handler for messages received from the protocol
  // engine. The address for the session layer server is left uninitialised,
  // which may create a crash if no session server is registered.

public:

	// The constructor initialises the base classes and registers the the
	// outbound message handler.

	NetworkLayer( std::string ServerName = "NetworkLayerServer" )
	: Actor( ServerName ), StandardFallbackHandler( ServerName )
	{
		RegisterHandler( this, &NetworkLayer< ValidLinkMessage ExternalMessa >::ResolveAddress  );
		RegisterHandler( this, &NetworkLayer< ValidLinkMessage ExternalMessa >::ResolvedAddress );
		RegisterHandler( this, &NetworkLayer< ValidLinkMessage ExternalMessa >::ActorRemoval    );
		RegisterHandler( this, &NetworkLayer< ValidLinkMessage ExternalMessa >::OutboundMessage );
		RegisterHandler( this, &NetworkLayer< ValidLinkMessage ExternalMessa >::Stop            );
	}

  // Finally there is a virtual destructor

  virtual ~NetworkLayer( void )
  { };
};


}      // End name space Theron
#endif // THERON_NETWORK_LAYER
