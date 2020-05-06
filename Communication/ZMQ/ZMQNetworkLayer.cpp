/*==============================================================================
Zero Message Queue (ZMQ) Network Layer

This file implements the functionality of the ZMQ network layer classes.
Please see the header file for details.

Author and Copyright: Geir Horn, University of Oslo, 2017-2019
License: LGPL 3.0
==============================================================================*/

#include "Communication/ZMQ/ZMQNetworkLayer.hpp"

/*=============================================================================

 Peer management

=============================================================================*/
//
// A new peer means another ZeroMQ link server hosted on a remote node serving
// that network endpoint.

void Theron::ZeroMQ::NetworkLayer::AddNewPeer(
	const Theron::ZeroMQ::NetworkAddress & Peer )
{
	// First commands socket is connected to the peer's discovery publisher
	// so that discovery commands from this endpoint can be received and
	// processed.

	Commands.connect( NetworkAddress( TCPAddress( Peer.GetIP(), DiscoveryPort ),
																		Peer.GetActorName() ) );

	// Then an outbound socket is created and connected to the inbound port of
	// the remote peer. An iterator to the new socket element is returned from
	// the emplace function (or an iterator to the existing socket) and this
	// is used to connect the socket.

	NetworkAddress PeerInbound( TCPAddress( Peer.GetIP(), InboundPort ),
															Peer.GetActorName() );

	auto NewSocket = Outbound.try_emplace( PeerInbound,
										 zmqpp::socket( ZMQContext, zmqpp::socket_type::dealer )
									 ).first;

	NewSocket->second.connect( PeerInbound );
}

// The shut down handler is called to ensure that all sockets are properly
// disconnected and closed. It is called after all local actors with external
// presence has been asked to shut down, and so the remote endpoints should
// have cleared references to the actors. As there are no more local actors
// with an external presence, there shall be no external communication and
// it should be safe to close all sockets.

void Theron::ZeroMQ::NetworkLayer::Stop(
	const Network::ShutDown & StopMessage, const Theron::Actor::Address Sender )
{
	// First inform all peers that this endpoint is closing

	OutsideMessage Goodbye( OutsideMessage::Type::Closing,
													OwnAddress, NetworkAddress() );

	Goodbye.Send( Discovery );

	// Disconnecting the Command socket and all outbound sockets

	for ( auto & RemoteEndpoint : Outbound )
  {
		Commands.disconnect( RemoteEndpoint.first );
	}
}

/*=============================================================================

 Message handling

=============================================================================*/
//
// The outbound message handler receives an outside message from the session
// layer, and forwards the message to the right receiver endpoint based on
// the message type. All messages should be messages since the command messages
// are handled by other dedicated functions.

void Theron::ZeroMQ::NetworkLayer::OutboundMessage(
	const Theron::ZeroMQ::OutsideMessage & TheMessage,
	const Theron::Actor::Address From )
{
	switch ( TheMessage.GetType() )
	{
		case OutsideMessage::Type::Message :
			TheMessage.Send( Outbound.at( TheMessage.GetRecipient() ) );
			break;
		default:
		{
			std::ostringstream ErrorMessage;

			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "ZeroMQ::NetworkLayer: message with illegal type "
									 << TheMessage.Type2String() << " from sender actor "
									 << TheMessage.GetSender().GetActorName() << " to actor "
									 << TheMessage.GetRecipient().GetActorName()
									 << " on endpoint " << TheMessage.GetRecipient().GetEndpoint()
									 << " with payload " << TheMessage.GetPayload();

		  throw std::invalid_argument( ErrorMessage.str() );
		}
	};
}

// The inbound message is received on a router socket that will add a frame
// indicating who the sender is. However, this is a random sender ID generated
// by the router socket and the real sender IP is encoded in the message.
// The first frame will therefore be discarded before the message is forwarded
// to the session layer for further processing (normal message). Address
// resolution messages are also just forwarded to the session layer server.
//
// The message could also be a subscribe message for which this endpoint
// should connect and subscribe to the endpoint sending the request, broadcast
// the same request via its publisher for the other peers to connect; before
// returning the list of known peers back to the newly connected peer.
//
// If the message is a roster, it is the instructions that this peer should
// connect to the peers whose addresses are given in the roster.

void Theron::ZeroMQ::NetworkLayer::InboundMessage(
	const Theron::ZeroMQ::NetworkLayer::NewInboundMessage & TheIndicator,
	const Theron::Actor::Address From )
{
	std::string    DevNull;
	zmqpp::message ReceivedMessage;

	Inbound.receive( ReceivedMessage );
	ReceivedMessage >> DevNull;

	OutsideMessage TheMessage( ReceivedMessage );

	switch ( TheMessage.GetType() )
  {
		case OutsideMessage::Type::Message :
			// A normal message is just forwarded to the session server so that the
			// local actor can be identified and the message forwarded to that actor.

			Send( TheMessage,
						Network::GetAddress( Network::Layer::Session ) );

			break;
		case OutsideMessage::Type::Subscribe :
			if ( Outbound.find( TheMessage.GetSender() ) == Outbound.end() )
			{
				// The peer is not known at this endpoint, and first a subscription
				// message is sent on its behalf to all other known peers. Note that
				// the new endpoint has already subscribed to the discovery channel
				// of this endpoint and so it will get the message too and need to
				// test to avoid connecting to itself.

				OutsideMessage NewPeerNotification( OutsideMessage::Type::Subscribe,
																						NetworkAddress(),
																						TheMessage.GetSender() );

				NewPeerNotification.Send( Discovery );

				// The next thing is to prepare the list of all known endpoints to
				// send back to the new endpoint for it to connect to these endpoints
				// The full protocol string will be sent to avoid issues.

				std::ostringstream Roster;

				for ( auto & KnownPeer : Outbound )
					Roster << KnownPeer.first.GetActorName() << " "
					       << KnownPeer.first.AsString() << " ";

				// Add and connect the new peer. This has to be done after creating
				// the roster message since the new peer will be added to the outbound
				// map when added, and doing this before creating the rooster string
				// would imply it would get its own address back.

				AddNewPeer( TheMessage.GetSender() );

				// Create and send the roster message

				OutsideMessage RosterMessage( OutsideMessage::Type::Roster,
																			OwnAddress, TheMessage.GetSender(),
																			Roster.str() );

				RosterMessage.Send( Outbound.at( TheMessage.GetSender() ) );
			}
			break;
		case OutsideMessage::Type::Roster :
		{
			// The IPs of the peers are read off the message payload and added
			// as new peers in the system. There is no natural actor name to give
			// the remote endpoint, and its actor address is therefore given as the
			// default Null address.

			std::istringstream RosterString( TheMessage.GetPayload() );

			while ( !RosterString.eof() )
			{
				std::string ActorName, PeerIP;

				RosterString >> ActorName >> std::ws >> PeerIP >> std::ws;
				AddNewPeer( NetworkAddress( PeerIP, ActorName ) );
			}
			break;
		}
		default:
			// The message is just forgotten. A data message should not be received
			// on this inbound port.
			break;
	}
}

// The hander for the subscribe socket can receive 5 different types of
// messages:
// 1. The discovery message to check if a named actor is hosted on this
//    endpoint; or
// 2. An address message containing the response of a previous discovery
//    request. An address message will be published to all endpoints by the
//    endpoint hosting the named actor. The idea is that the session layer
//    server will cache the external addresses of all known actors to save
//    discovery time if one of the local actors would like to send a message
//    to this actor; or
// 3. An actor removal message indicating that an actor whose address was
//    previously published is closing down and should no longer be cached by
//    the session layer servers; or
// 4. A subscribe message sent from the helper peer of a newly connected
//    endpoint. As the newly connected endpoint is connected to the publisher
//    channel of the helper, it will also get this message and should not
//    connect to the newly connected endpoint, i.e. itself. All other endpoints
//    will connect to the newly connected endpoint; or
// 5. A message published from an endpoint when it is closing. In this case
//    all actors on that endpoint should have sent removal messages, and so
//    an endpoint may just forget about the remote endpoint closing.

void Theron::ZeroMQ::NetworkLayer::CommandMessage(
	const Theron::ZeroMQ::NetworkLayer::NewCommandMessage & TheIndicator,
	const Theron::Actor::Address From )
{
	zmqpp::message ReceivedMessage;

	Commands.receive( ReceivedMessage );

	OutsideMessage TheMessage( ReceivedMessage );

	switch ( TheMessage.GetType() )
  {
		case OutsideMessage::Type::Discovery :
			// An address message is fundamentally a question to the session layer
			// server if the named actor is hosted on this network endpoint. Hence,
			// the message is forwarded to the session layer as a local actor
			// inquiry.

			Send( ResolutionRequest( TheMessage.GetRecipient().GetActorAddress() ),
						Network::GetAddress( Network::Layer::Session ) );

			break;
		case OutsideMessage::Type::Address :
			// An request for address resolution has been completed with the endpoint
			// hosting the actor responding with the actor's address that is indicated
			// as the sender of the message. It will be sent to the session layer
			// as a resolution response message.

			Send( ResolutionResponse( TheMessage.GetSender(),
															  TheMessage.GetSender().GetActorAddress() ),
																Network::GetAddress( Network::Layer::Session )  );
			break;
		case OutsideMessage::Type::Remove :
			Send( RemoveActor( TheMessage.GetSender() ),
						Network::GetAddress( Network::Layer::Session ) );
			break;
		case OutsideMessage::Type::Subscribe :
			// The protocol is that the new peer connects to one given peer
			// of the system and that peer will then send out a subscribe
			// message on its Discovery channel on behalf of the new peer
			// asking the already connected peers to connect to the new peer.
			// However the new peer is also connected to the discovery channel
			// of the given peer helping it to be connected, and so it is
			// necessary to check if the incoming message is a request to
			// connect this peer to this peer, i.e. this is the new peer
			// which should obviously not be connected to itself.

			if ( TheMessage.GetSender().GetIP() != OwnAddress.GetIP() )
				AddNewPeer( TheMessage.GetSender() );
			break;
		case OutsideMessage::Type::Closing :
			// All actors of the closing endpoint should already be removed, and
			// this endpoint can just unsubscribe from the closing endpoint and
			// delete the outbound socket for the endpoint.
		  Commands.unsubscribe( TheMessage.GetSender() );
			Outbound.erase( TheMessage.GetSender() );
			break;
		default:
			// The messages of type Rooster, Message, and Data should
			// never be sent on the discovery channel and should never be
			// subject to processing by this handler. A logic error exception
			// will be created for this case.
			{
				std::ostringstream ErrorMessage;

				ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
				             << "ZMQ Network Layer received a message on the "
										 << "command channel of the wrong type.";

			  throw std::logic_error( ErrorMessage.str() );
			}
			break;
	}
}

/*=============================================================================

 Actor address management

=============================================================================*/
//
// If the sender is an actor on the local endpoint, the external address is
// returned as the actors network address on this endpoint. Since this is an
// actor with external presence its availability is also announced on the
// discovery channel so that Session Layer servers on remote endpoints may
// store the actors address saving the time to discover it when one of its
// local actors wants to send a message to this actor.
//
// If the request is a discovery request for a remote actor because one of
// the actors on this endpoint has a message for the remote actor, things get
// slightly more complicated. Then an address request is constructed and
// published on the broadcast socket, and one needs to wait for the returned
// resolved address. This will be forwarded to the session layer by the
// command handler.

void Theron::ZeroMQ::NetworkLayer::ResolveAddress(
const Theron::NetworkLayer<Theron::ZeroMQ::OutsideMessage>::ResolutionRequest &
	TheRequest, const Theron::Actor::Address TheSessionLayer )
{
	if ( TheRequest.NewActor.IsLocalActor() )
  {
		NetworkAddress ActorsExternalAddress( OwnAddress.GetEndpointLocation(),
																	        TheRequest.NewActor );

		Send( ResolutionResponse( ActorsExternalAddress, TheRequest.NewActor ),
					TheSessionLayer );

		OutsideMessage
		NewLocalActor( OutsideMessage::Type::Address, ActorsExternalAddress,
		  NetworkAddress( OwnAddress.GetEndpointLocation(), TheSessionLayer ) );

		NewLocalActor.Send( Discovery );
	}
	else
  {
		// The request is sent as if it was from the "session layer" which normally
		// corresponds to the session layer, but it could be any other actor
		// knowing about this resolution protocol.

		OutsideMessage AddressRequest( OutsideMessage::Type::Discovery,
		    NetworkAddress( OwnAddress.GetEndpointLocation(), TheSessionLayer ),
				NetworkAddress( TCPAddress(), TheRequest.NewActor ) );

		AddressRequest.Send( Discovery );
	}
}

// When an external address inquiry has been handled by the session layer and
// the requested actor is a local actor, then its external address is returned
// from the session layer. The external address will then be dispatched on the
// discovery channel so that all session layers may be informed about the
// location of this actor and a future lookup is not needed if the same actor
// is requested by and actor on an endpoint different from the one making this
// request.

void Theron::ZeroMQ::NetworkLayer::ResolvedAddress(
const Theron::NetworkLayer<Theron::ZeroMQ::OutsideMessage>::ResolutionResponse &
	TheResponse, const Theron::Actor::Address TheSessionLayer )
{
	OutsideMessage
	AddressReply( OutsideMessage::Type::Address, TheResponse.GlobalAddress,
	  NetworkAddress( OwnAddress.GetEndpointLocation(), TheSessionLayer ) );

	AddressReply.Send( Discovery );
}

// When a local actor is closing the removal is broadcast to all the other
// actors. This because there can be actors on remote nodes that have sent
// messages to this actor, and the session layer needs to be informed that it
// is no longer existing for doing the correct housekeeping.

void Theron::ZeroMQ::NetworkLayer::ActorRemoval(
const Theron::NetworkLayer< Theron::ZeroMQ::OutsideMessage >::RemoveActor &
	TheCommand, const Theron::Actor::Address TheSessionLayer )
{
	OutsideMessage
	RemovalCommand( OutsideMessage::Type::Remove, TheCommand.GlobalAddress,
		NetworkAddress( OwnAddress.GetEndpointLocation(), TheSessionLayer ) );

	RemovalCommand.Send( Discovery );
}

/*=============================================================================

 Constructor

=============================================================================*/
//
// The constructor initialises the various sockets and starts the connection
// to the other peer endpoints of the actor system if this is not the first
// endpoint indicated by having the local host as IP address.

Theron::ZeroMQ::NetworkLayer::NetworkLayer(
	const Theron::ZeroMQ::IPAddress & InitialRemoteEndpointIP,
	const std::string & RemoteNetworkLayerServerName,
	const std::string & ServerName )
: Actor( ServerName ), StandardFallbackHandler( GetAddress().AsString() ),
  Theron::NetworkLayer< OutsideMessage >( GetAddress().AsString() ),
  ZMQContext(),
  Discovery ( ZMQContext, zmqpp::socket_type::publish   ),
  Commands  ( ZMQContext, zmqpp::socket_type::subscribe ),
  Inbound   ( ZMQContext, zmqpp::socket_type::router    ),
  Outbound()
{
	// First the handlers for inbound messages and command messages are registered

	RegisterHandler( this, &NetworkLayer::InboundMessage );
	RegisterHandler( this, &NetworkLayer::CommandMessage );

	// The global network address of this endpoint is fixed first. Note that by
	// using the wild card notation for the IP it means that they will listen for
	// connections on any network interface of the host computer ("tcp://*"), and
	// if the initial remote endpoint ID is empty, it means that Inter Process
	// Communication should be used as the protocol to connect to other peer
	// network layer servers in the actor system.

	if ( InitialRemoteEndpointIP == IPAddress() )
		OwnAddress = NetworkAddress( TCPAddress( IPAddress(), InboundPort ),
																 GetAddress() );
	else
		OwnAddress = NetworkAddress( TCPAddress("*", InboundPort ),
																 GetAddress() );

	// The sockets accepting connections are bound to the ports.
	// This is useful if the host computer has more than one network interface.
	// Furthermore, the own address constructed above will contain the IP address
	// of only one of these these adapters, and this IP address may not be the
	// same IP address as returned from a DNS lookup for this machine. On the
	// other hand, the own address cannot be set to the address of the machine
	// since there may not be a useful DNS registration for this machine (if the
	// IP is dynamically assigned by DHCP its DNS ID may just be the IP with a
	// temporary flag. It will therefore return a DNS lookup, but this address
	// is as valuable as having the raw IP address.)

	Discovery.bind( NetworkAddress(
		TCPAddress( OwnAddress.GetIP(), DiscoveryPort ), GetAddress() ) );

	Inbound.bind( OwnAddress );

	// Then the receiving sockets are added to the socket monitor ensuring that
	// the right handler will be invoked when they get a message. Lambda functions
	// must be used since the member functions cannot be used as function pointers
	// without the 'this' pointer or an object for which the pointer can be called

	SocketMonitor.add( Commands, [this](void)->void{
		Actor::Send( NewCommandMessage(), GetAddress(), GetAddress() ); } );

	SocketMonitor.add( Inbound, [this](void)->void{
		Actor::Send( NewInboundMessage(), GetAddress(), GetAddress() ); } );

	// The first frame received on the subscriber socket is the "topic" of the
	// published message. This corresponds to the message type of the outbound
	// message, and the socket subscribes to these topics. It is necessary to
	// construct a temporary outside since the function converting the type to
	// a string converts the type of the message it is called for.

	Commands.subscribe( OutsideMessage( OutsideMessage::Type::Address,
											 								 OwnAddress, OwnAddress ).Type2String() );
	Commands.subscribe( OutsideMessage( OutsideMessage::Type::Remove,
											 								 OwnAddress, OwnAddress ).Type2String() );
	Commands.subscribe( OutsideMessage( OutsideMessage::Type::Subscribe,
											 								 OwnAddress, OwnAddress ).Type2String() );

	// This link server should connect to the initial remote peer if it is not
	// the local host, which would be the case if it is the first peer to start.

	if ( InitialRemoteEndpointIP != LocalHost() )
  {
		NetworkAddress FirstPeer( TCPAddress(InitialRemoteEndpointIP, InboundPort),
												      RemoteNetworkLayerServerName );

	  AddNewPeer( FirstPeer );

		// An initial subscribe request is then sent to the remote peer with the
		// IP of this endpoint as argument to trigger the initialisation and the
		// discovery of the other peers in the system.

		OutsideMessage SubcriptionRequest( OutsideMessage::Type::Subscribe,
																			 OwnAddress, FirstPeer,
				                               OwnAddress.GetIP().to_string() );

		SubcriptionRequest.Send( Outbound.at( FirstPeer ) );
	}
}
