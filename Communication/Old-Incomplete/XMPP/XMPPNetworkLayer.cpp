/*=============================================================================
  Network Layer

  This file implements the network layer defined in the corresponding header.

  Author and copyright: Geir Horn, University of Oslo, 2015 - 2020
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include <functional>   // Reference to class member functions
#include <algorithm>    // Find in vectors
#include <sstream>      // Formatted streams
#include <stdexcept>    // Standard exceptions

// Theron++ files defining the network layer interface to be implemented

#include "Communication/XMPP/XMPPNetworkLayer.hpp"
#include "Communication/XMPP/XMPPEndpoint.hpp"

/*==============================================================================

 Utility functions

==============================================================================*/
//
// The new peer function simply looks up the local ID, and calls the client
// record's new peer function if there is a client record for this ID.
// Otherwise it throws an invalid argument exception.

void Theron::XMPP::NetworkLayer::NewPeer(
	const Theron::XMPP::JabberID & LocalID,
	const Theron::XMPP::JabberID & RemoteID )
{
	auto TheClientRecord = Clients.find( LocalID );

  if ( TheClientRecord != Clients.end() )
    TheClientRecord->second.NewPeer( RemoteID );
  else
  {
    std::ostringstream ErrorMessage;

    ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
						     << "JID = " << RemoteID << " is new peer for unknown "
								 << "local JID = " << LocalID;

    throw std::invalid_argument( ErrorMessage.str() );
  }
}

// This function is an interface to the corresponding function defined in
// the client record: The new peer first registers the peer as an active peer
// for this client and then calls the send function when emptying the queue of
// messages that have been waiting for the remote actor to become available.

void Theron::XMPP::NetworkLayer::ClientRecord::NewPeer(
	const Theron::XMPP::JabberID & ThePeer )
{
  ActivePeers.insert( ThePeer );

  if ( !MessageQueue.empty() )
  {
    auto MessageRange = MessageQueue.equal_range( ThePeer );

    for ( auto aMessage = MessageRange.first; aMessage != MessageRange.second;
						 ++aMessage )
      SendMessage( aMessage->second );

    MessageQueue.erase( MessageRange.first, MessageRange.second );
  }
}

// In a similar way will the remove peer function throw if the local peer is
// not available, otherwise it delegates to the remove peer function of the
// client record.

void Theron::XMPP::NetworkLayer::RemovePeer(
	const Theron::XMPP::JabberID & LocalID,
	const Theron::XMPP::JabberID & RemoteID )
{
	auto TheClientRecord = Clients.find( LocalID );

  if ( TheClientRecord != Clients.end() )
    TheClientRecord->second.RemovePeer( RemoteID );
  else
  {
    std::ostringstream ErrorMessage;

    ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
						     << "JID = " << RemoteID << " cannot be removed from unknown "
								 << "local JID = " << LocalID;

    throw std::invalid_argument( ErrorMessage.str() );
  }
}

// At the moment a peer indicates that it is no longer available, it should
// be removed from the list of active peers. In this case there should of
// course not be any messages waiting, so it is sufficient just to remove the
// peer.

void Theron::XMPP::NetworkLayer::ClientRecord::RemovePeer(
	const Theron::XMPP::JabberID & ThePeer )
{
	ActivePeers.erase( ThePeer );
}

/*==============================================================================

 Event handlers

==============================================================================*/
//
// The first event is that a client has been registered with the local XMPP
// server and this will trigger the connection of the client.

void Theron::XMPP::NetworkLayer::ClientRegistered(
	Theron::XMPP::JabberID ClientID,
	Swift::Payload::ref RegistrationResponse, Swift::ErrorPayload::ref Error )
{
  if ( Error == nullptr )
  {
    auto TheClientRecord = Clients.find( ClientID );

    if ( TheClientRecord != Clients.end() )
    {
      // The client is now ready to be connected to the XMPP network. Note
      // that the handler for the connect event was already registered when
      // the client record was created.

      TheClientRecord->second.TheClient->connect( ClientsOptions );
    }
  }
}

// When the call-back is received that a client is connected, it will
// immediately request the roster with the initial roster function handling
// the callback.

void Theron::XMPP::NetworkLayer::ClientConnected(
	Theron::XMPP::JabberID ClientID )
{
  RosterRequest( ClientID,
    [=](Swift::RosterPayload::ref TheRoster, Swift::ErrorPayload::ref Error)
       { InitialRoster( ClientID, TheRoster, Error ); }
  );
}

// Then there is a support function to send a presence message to a remote
// recipient. It may or may not take a Jabber ID for the destination, and the
// absence of a Jabber ID for the receiver indicates that the presence should
// be sent to the server (to trigger potential responses from other connected
// actors)
//
// It should be noted that the each presence must be directed, in other words
// it must have a destination. If the destination is invalid, the presence
// will not be sent.

void Theron::XMPP::NetworkLayer::SendPresence(
	Theron::XMPP::JabberID FromClient, Swift::Presence::Type PresenceType,
	Theron::XMPP::JabberID ToClient )
{
	auto TheClientRecord = Clients.find( FromClient );

  if ( TheClientRecord != Clients.end() )
  {
    Swift::Presence::ref TheClientPresence = Swift::Presence::create();

    TheClientPresence->setFrom( FromClient );
    TheClientPresence->setType( PresenceType );

    if( TheClientRecord->second.Priority )
      TheClientPresence->setPriority( *(TheClientRecord->second.Priority) );
    else
      TheClientPresence->setPriority( 0 );

    if ( ToClient.isValid() )
      TheClientPresence->setTo( ToClient );

    TheClientRecord->second.TheClient->sendPresence( TheClientPresence );
  }
}

// The presence information leads to a handshake among the actors when one
// actor wants to subscribe to availability information from another actor.
// The handler must therefore deal with both incoming requests for subscription
// and confirmations from the remote party that an actor has successfully
// subscribed.

void Theron::XMPP::NetworkLayer::PresenceNotification(
	Theron::XMPP::JabberID ClientID, Swift::Presence::ref PresenceReceived )
{
  switch ( PresenceReceived->getType() )
  {
    case Swift::Presence::Subscribe :
      SendPresence( ClientID, Swift::Presence::Subscribed,
								    PresenceReceived->getFrom() );
      SendPresence( ClientID, Swift::Presence::Subscribe,
								    PresenceReceived->getFrom() );
      break;
    case Swift::Presence::Subscribed :
      SendPresence( ClientID, Swift::Presence::Available,
								    PresenceReceived->getFrom() );
      break;
    case Swift::Presence::Unsubscribe :
      SendPresence( ClientID, Swift::Presence::Unsubscribed,
								    PresenceReceived->getFrom() );
      break;
    case Swift::Presence::Available :
      // The remote actor is now available and it should be registered with
			// the session layer as an available actor.

		  Send( ResolutionResponse( PresenceReceived->getFrom(),
						 						 Address( PresenceReceived->getFrom().getResource() ) ),
						Network::GetAddress( Network::Layer::Session ) );

      // Then the new actor should also be known to the client, and if
      // messages have been buffered waiting for this actor to become active
      // they can now be forwarded.

      NewPeer( ClientID, PresenceReceived->getFrom() );
      break;
    case Swift::Presence::Unavailable :
      // Similar to the availability status, also unavailability statuses
      // from endpoint resources should be ignored.

      if ( PresenceReceived->getFrom().getResource() != "endpoint" )
				Send( RemoveActor( PresenceReceived->getFrom() ),
							Network::GetAddress( Network::Layer::Session ) );

			// The local client must also remove references to the disappearing peer

      RemovePeer( ClientID, PresenceReceived->getFrom() );
      break;
    case Swift::Presence::Error :
      break;
    case Swift::Presence::Probe :
      break;
    case Swift::Presence::Unsubscribed :
      break;
    default:
      // Still not doing anything!
      break;
  }
}

// The initial roster is just a refresh and will be used to tell the actors on
// the roster that this actor has become available.

void Theron::XMPP::NetworkLayer::InitialRoster(
	Theron::XMPP::JabberID ClientID,
	Swift::RosterPayload::ref TheRoster, Swift::ErrorPayload::ref Error )
{
  if (!Error )
    for ( auto RemoteActor : TheRoster->getItems() )
     SendPresence( ClientID, Swift::Presence::Available, RemoteActor.getJID() );
}

// Dispatching the roster means reading off each of the connected IDs and
// send them back to the actor waiting for these.

void Theron::XMPP::NetworkLayer::DispatchRoster(
	const Theron::Actor::Address WaitingActor,
	Swift::RosterPayload::ref TheRoster, Swift::ErrorPayload::ref Error )
{
  if (!Error )
  {
    Roster CurrentPeers;

    for ( auto RemoteActor : TheRoster->getItems() )
      CurrentPeers.insert( RemoteActor.getJID() );

    Send( CurrentPeers, WaitingActor );
  }
}

/*==============================================================================

 Actor discovery

==============================================================================*/
//
// The dispatch known peers method is the callback registered by a network
// endpoint when it requests its roster on behalf of a newly joined endpoint.
// When the roster arrives it will contain the jabber IDs of the agents known
// to this endpoint, and these will directly be forwarded to the new endpoint
// as subscribe messages for it to subscribe to all known agents.
//
// Note that the actor address of the sender in this case has no importance
// since the address is encoded in the external message. It is given as the
// link address to satisfy the requirements of the outbound message handler.

void Theron::XMPP::NetworkLayer::DispatchKnownPeers(
	Theron::XMPP::JabberID ThisEndpoint, Theron::XMPP::JabberID NewEndpoint,
	Swift::RosterPayload::ref TheRoster, Swift::ErrorPayload::ref Error )
{
  if ( !Error )
   for ( auto RemoteActor : TheRoster->getItems() )
     OutboundMessage( ExternalMessage( ThisEndpoint, NewEndpoint,
				  RemoteActor.getJID().toString(),
				  "SUBSCRIBE" ), GetAddress() );
}

// The new endpoint will register the subscribe known peers method as its
// handler for incoming messages. It will first check that the received message
// is really a subscription message, and if it is, then a subscription will be
// made to the remote actor whose Jabber ID is in the message body. It will
// throw if the message is not a subscribe message since this situation should
// not occur.

void Theron::XMPP::NetworkLayer::SubscribeKnownPeers(
	Theron::XMPP::JabberID NewEndpoint, Swift::Message::ref XMPPMessage )
{
  if ( XMPPMessage->getSubject() == "SUBSCRIBE" )
    SendPresence( NewEndpoint, Swift::Presence::Type::Subscribe,
                  JabberID( XMPPMessage->getBody().value() ) );
  else
  {
    std::ostringstream ErrorMessage;

    ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
						     << "Subscribe known peers expected SUBSCRIBE as subject "
								 << "and got subject " << XMPPMessage->getSubject()
								 << " and body = [ " << XMPPMessage->getBody() << "]";

    throw std::invalid_argument( ErrorMessage.str() );
  }
}

// The endpoint presence is almost identical to the normal presence handler,
// at the exception that it will check if the subscription is from an endpoint,
// and in that case return the roster to the remote subscriber. It will also
// intercept the subscribed message to avoid that the network endpoint indicates
// its presence as active.

void Theron::XMPP::NetworkLayer::EndpointPresence(
	Theron::XMPP::JabberID ClientID, Swift::Presence::ref PresenceReceived )
{
  // First the normal presence handling and handshake is performed

  PresenceNotification( ClientID, PresenceReceived );

  // Then if this is a subscription, it will lead to the return of the roster
  // to the network endpoint subscribing

  if ( PresenceReceived->getType() == Swift::Presence::Subscribe )
  {
    JabberID NewEndpoint( PresenceReceived->getFrom() );

    RosterRequest( ClientID,
      [=]( Swift::RosterPayload::ref TheRoster,
				   Swift::ErrorPayload::ref  Error )->void{
      DispatchKnownPeers( ClientID, NewEndpoint, TheRoster, Error );
    });
  }
}

/*==============================================================================

 Command handlers

==============================================================================*/
//
// The address resolution will check if the given actor is a local actor, and
// and if this is the case it will create the client for this actor and connect
// it to the XMPP server. Note that the function is robust, in the sense that
// if it is requested that a client is created that does already exist, it will
// not create a new client.
//
// An interesting point is that under the XMPP protocol all actors will be
// known everywhere. They will be registered with the session layer server
// once their presence is set to "available". As a consequence, the session
// layer server should never request an address resolution for a remote actor.
// If it does, it means that a local actor is trying to send a message to a
// remote actor that has not yet completed the registration. This request will
// be ignored because no messages can be sent to the remote actor before it
// becomes available.

void Theron::XMPP::NetworkLayer::ResolveAddress(
const Theron::NetworkLayer< Theron::XMPP::ExternalMessage >::ResolutionRequest
	& TheRequest, const Theron::Actor::Address TheSessionLayer )
{
	JabberID ActorJID( ProtocolID.getNode(), ProtocolID.getDomain(),
								     TheRequest.NewActor.AsString() );

	// If the requesting actor is an actor on this endpoint, its address can
	// simply be returned to the session layer.

	if ( TheRequest.NewActor.IsLocalActor() )
  {
		Send( ResolutionResponse( ActorJID, TheRequest.NewActor ),
					TheSessionLayer );

		// Then the XMPP client should be created and connected if this local
		// actor is not already known.

		if ( Clients.find( ActorJID ) == Clients.end() )
	  {
	    auto TheClient = std::make_shared< Swift::Client >(
			     ActorJID, ServerPassword, &NetworkManager );

	    if ( ClientsOptions.useTLS == Swift::ClientOptions::NeverUseTLS )
	      TheClient->setAlwaysTrustCertificates();

	    // Setting event handler for the presence messages

	    TheClient->onPresenceReceived.connect(
	      [=] (Swift::Presence::ref PresenceMessage)->void {
					  PresenceNotification( ActorJID, PresenceMessage );
	      });

	    // As this client serving a normal actor should just announce its
	    // availability when it is connected since the subscription to other
	    // endpoints should already be done by the endpoint client. The
			// availability message should be a broadcast to all other endpoints, and
			// it is therefore sent with no destination.

	    TheClient->onConnected.connect(
	      [=] (void)->void {
						SendPresence( ActorJID, Swift::Presence::Available );
	      });

	    // If the client has a resource field it is an actor with its own set of
	    // messages it can accept, and then the standard message processing
	    // handler is called. Basically this will just forward the message to
	    // the session layer so that the addresses can be correctly decoded and
	    // the message payload forwarded to the right local actor.
			//
			// Note that the send is impersonated to come from the remote actor as
			// defined in the Jabber ID resource.

	    TheClient->onMessageReceived.connect(
	      [=](Swift::Message::ref XMPPMessage)->void {
					  Send( ExternalMessage( XMPPMessage->getFrom(), XMPPMessage->getTo(),
					                         XMPPMessage->getBody().value(),
					                         XMPPMessage->getSubject() ),
									Network::GetAddress( Network::Layer::Session )
								);
	      });

	    // All parts of the client record has now been initialised and it can be
	    // stored in the internal map of connected clients with default presence
			// priority.

	    Clients.emplace( ActorJID,
									     ClientRecord( TheClient, PrecencePriority() ) );

	    // Then the client can be connected and the handlers should deal with the
	    // future events.

	    TheClient->connect( ClientsOptions );
	  }
	}
}

// The handler for resolution responses from the session layer should never
// be used, and it will therefore only throw a logic error if invoked.

void Theron::XMPP::NetworkLayer::ResolvedAddress(
const Theron::NetworkLayer< Theron::XMPP::ExternalMessage >::ResolutionResponse
	& TheResponse, const Theron::Actor::Address TheSessionLayer )
{
	std::ostringstream ErrorMessage;

	ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
	             << "XMPP Network Layer has received a resolution response with "
							 << " the external address " << TheResponse.GlobalAddress
							 << " for actor " << TheResponse.TheActor.AsString() << " from "
							 << "the Session Layer " << TheSessionLayer.AsString()
							 << ". This should never happen and something is wrong in the "
							 << "implementation!";

  throw std::logic_error( ErrorMessage.str() );
}


// Destroying a client is simply to take it out of the client map and
// delete it. Nothing happens if the client does not exist. Before removing
// the client record it is necessary to inform all peer that this client is
// unavailable.

void Theron::XMPP::NetworkLayer::ActorRemoval(
const Theron::NetworkLayer< Theron::XMPP::ExternalMessage >::RemoveActor &
	TheCommand, const Theron::Actor::Address TheSessionLayer )
{
  auto CurrentClient = Clients.find( TheCommand.GlobalAddress );

  if ( CurrentClient != Clients.end() )
	{
		SendPresence( TheCommand.GlobalAddress, Swift::Presence::Unavailable );
		Clients.erase( CurrentClient );
	}
}

// Eventually, there will be a shut down message received indicating that
// the all local actors will have asked for removal, as per the above handler.
// In this case there should be only one client left to remove and that is the
// actor for the endpoint itself. However, should there be more clients left
// for some error, then these are also marked as unavailable and deleted.

void Theron::XMPP::NetworkLayer::Stop(
const Network::ShutDown & StopMessage, const Theron::Actor::Address Sender )
{
	for( auto & ActiveClient : Clients )
		SendPresence( ActiveClient.first, Swift::Presence::Unavailable );

	Clients.clear();
}

/*==============================================================================

 Normal message handlers

==============================================================================*/
//
// Sending a message to a remote actor implies first creating the XMPP message
// and setting the right fields, before dispatching the XMPP message on the
// right local client. If the message is not from this client an logic error
// exception will be thrown.

void Theron::XMPP::NetworkLayer::ClientRecord::SendMessage(
	const Theron::XMPP::ExternalMessage & Message )
{
  if ( TheClient->getJID() == Message.GetSender() )
    if ( ActivePeers.find( Message.GetRecipient() ) != ActivePeers.end() )
    {
      Swift::Message::ref
      XMPPMessage = Swift::Message::ref( new Swift::Message() );

      XMPPMessage->setType   ( Swift::Message::Chat );
      XMPPMessage->setTo     ( Message.GetRecipient() );
      XMPPMessage->setFrom   ( Message.GetSender() );
      XMPPMessage->setSubject( Message.GetSubject() );
      XMPPMessage->setBody   ( Message.GetPayload() );

      TheClient->sendMessage( XMPPMessage );
    }
    else
      MessageQueue.emplace( Message.GetRecipient(), Message );
  else
  {
    std::ostringstream ErrorMessage;

    ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
						     << "Send message called on client JID = "
								 << TheClient->getJID() << " with sender address JID = "
								 << Message.GetSender();

    throw std::logic_error( ErrorMessage.str() );
  }
}

// The network level send message method is invoked by the Network Layer when
// receiving a message for a remote actor. It will first look up the client
// corresponding to the sender's Jabber ID, and if then use the client's send
// message function. However, if the client does not exist, the message will
// simply be ignored.

void Theron::XMPP::NetworkLayer::OutboundMessage(
	const Theron::XMPP::ExternalMessage & TheMessage,
	const Theron::Actor::Address From )
{
  auto TheClientRecord = Clients.find( TheMessage.GetSender() );

  if ( TheClientRecord != Clients.end() )
    TheClientRecord->second.SendMessage( TheMessage );
}

/*==============================================================================

 Constructor and destructor

==============================================================================*/
//
// The constructor first starts the event manager being the simple event loop,
// then it starts the network manager passing it the address of the event
// manager before the event manager is started in the thread.

Theron::XMPP::NetworkLayer::NetworkLayer(
	const std::string & EndpointName, const std::string & EndpointDomain,
	const std::string & TheServerPassword,
	const Theron::XMPP::JabberID & InitialRemoteEndpoint,
	const std::string & ServerName )
: Actor( ServerName ),
  StandardFallbackHandler( GetAddress().AsString() ),
  Theron::NetworkLayer< ExternalMessage >( GetAddress().AsString() ),
  EventManager(), NetworkManager( &EventManager ),
  CommunicationLink( &Swift::SimpleEventLoop::run, std::ref(EventManager) ),
  ProtocolID( EndpointName, EndpointDomain, GetAddress().AsString() ),
  ServerPassword( TheServerPassword )
{
  // Setting options for the clients. In this version we assume a secure
  // and trusted network so we will not use TLS, and if TLS is switched off,
  // then the clients will also always trust the provided certificates.

  ClientsOptions.allowPLAINWithoutTLS = true;
  ClientsOptions.useTLS = ClientsOptions.Swift::ClientOptions::NeverUseTLS;

	// There is no need to register the various message handlers for the interface
	// with the session layer server since these have already been registered by
	// the network layer server.

  // ENDPOINT CLIENT CREATION
  //
  // This new endpoint will have a client dedicated to actor discovery. It
  // will connect to the local XMPP server before sending its presence to the
  // initial remote peer given. This initial remote peer will then send back
  // the list of all agents it knows about (should be all agents connected to
  // the system), and then this endpoint will subscribe to them too and thereby
  // become fully connected.
  //
  // The creation of this client is similar to what is done by the Create
  // Client message handler, with different event handlers so it will not help
  // clarity to reuse create client handler.

  auto TheClient = std::make_shared< Swift::Client >(
		   ProtocolID, ServerPassword, &NetworkManager );

  if ( ClientsOptions.useTLS == Swift::ClientOptions::NeverUseTLS )
    TheClient->setAlwaysTrustCertificates();

  // Setting special event handler for the presence messages that will take
  // care of forwarding the roster of this endpoint to other endpoints that
  // may come available in the future.

  TheClient->onPresenceReceived.connect(
    [=] (Swift::Presence::ref PresenceMessage)->void {
				EndpointPresence( ProtocolID, PresenceMessage );
    });

  // Then this network endpoint will simply subscribe to this known endpoint
  // as soon as it is connected to the local XMPP server. However, it could
  // be that this endpoint is the very first endpoint in the system and
  // in this case the known endpoint address will be invalid, and nothing
  // should happen when we connect.

  if ( InitialRemoteEndpoint.isValid() )
    TheClient->onConnected.connect( [=](void)->void {
					SendPresence( ProtocolID, Swift::Presence::Type::Subscribe,
		      InitialRemoteEndpoint );
    });

  // A network endpoint should only receive messages from other network
  // endpoints asking the new network endpoint to subscribe to the other
  // known endpoints. Hence we bind the hander to make the subscriptions
  // as the message handler.

  TheClient->onMessageReceived.connect(
    [=](Swift::Message::ref XMPPMessage)->void {
				SubscribeKnownPeers( ProtocolID, XMPPMessage );
    });

  // All parts of the client record has now been initialised and it can be
  // stored in the internal map of connected clients. A subtle point is that
  // this client is registered with +128 as priority. This priority will be
  // used for all presence messages sent from this client. A higher priority
  // means that the client is more likely to receive messages that are sent
  // with a bare Jabber ID, typically the subscribe message from joining peers.

  Clients.emplace( ProtocolID, ClientRecord( TheClient, 128 ) );

  // The first client must be registered before it can be connected and used.
  // Other actors will just be resources on this network endpoint, and will
  // therefore not need a separate registration with the server.

  TheClient->connect( ClientsOptions );
}

// The destructor will first ask the event manager to stop, and then wait for
// the thread running the manger to terminate.

Theron::XMPP::NetworkLayer::~NetworkLayer()
{
  EventManager.stop();
  CommunicationLink.join();
}

