/*==============================================================================
Zero Message Queue (ZMQ) Network Layer

The Network layer is responsible for the implementation of the endpoint to
endpoint protocol and the management of the ZMQ sockets and library.

There are three types of communication to consider:

1. Discovery and control: These are messages exchanged between endpoints to
   accept new endpoints to the actor system, and to resolve the location of
   actors by name across the endpoints of the distributed actor system.
   Each endpoint will have one Discovery socket which is a ZMQ_PUB publisher
   socket sending the messages to all other endpoints. All other endpoints
   will connect a dedicated Command ZMQ_SUB socket to the discovery
   socket of all peers since there is no way to respond back to the published
   messages, The responses to commands will therefore have to come through
   the normal Inbound socket for the endpoint.

2. Actor-to-Actor messages representing the normal message exchange of the
   actor system. After resolving the endpoint hosting an actor, the dedicated
   outbound socket for this remote peer will be used. This socket is of the
   ZMQ_DEALER type. This socket will block if the messages cannot be sent,
	 and it is therefore important that closing endpoints inform the other
	 endpoints that they disappear so that the sockets for these endpoints can
	 be removed. the dealer socket will also distribute messages round-robin
	 to the connected peers, but this behaviour is not a problem in this case
	 since it is connected to only one destination peer. Inbound messages for a
	 node is received on a ZMQ_ROUTER socket.

3. Event communication: One of the strengths of ZMQ is its
   built-in support for one-to-many messaging where actors can subscribe
   to event messages from an actor, and when this update is sent each
   actor will get the update. In a purist view, it is not necessary to support
   this pattern at the network layer since the publishing actor should accept
   the subscriptions, keep a list of subscribers and send a message to each
   of them to "publish" the event using normal actor-to-actor messages. However,
	 since ZMQ supports this pattern, and many event driven systems use ZMQ
	 providing support for this pattern will allow actors to subscribe to data
	 published by a remote system. As this behaviour is not mandatory for an
	 actor system and it is therefore implemented as a derived class of the
	 network layer, see below.

When a new peer endpoint arrives to an actor system, it needs one other peer
to connect to (helper peer). Then the bootstrapping procedure is:
a. The new peer connects to the Discovery channel of the helper peer
b. The new peer will send a Subscribe message to the helper peer
c. The helper peer will forward the subscribe message to its discovery
   channel to make all peers already connected to it aware of the new
   peer.
d. The helper peer will send back a roster of all known peers to the new
   peer so that it can connect to them.
e. Any other peer receiving the subscription request from the new peer from
   the helper peer will connect to the new peer's discovery channel and
   to the Inbound channel of the new peer.
After this the peer-to-peer system is fully connected and actors can start
sending messages to actors on remote endpoints.'

It is currently assumed that the messages are passed via TCP, and if the
communicating parts of the actor system are hosted on the same physical node,
a loop back via the local host should be used. It may seem to be slow and
wasteful to use the TCP stack for such inter process communication when pure
inter process communication (IPC) is supported by ZMQ. However, different
operating systems implements IPC differently, and several of them will do a
network layer loop back behind the scene. The ZMQ direct IPC mechanism is
supported only on systems supporting Unix Domain Sockets only implemented for
POSIX compliant systems. More experimentation is needed for validating the
performance hit of using TCP loop back for IPC, and if support should be
provided.

Author and Copyright: Geir Horn, University of Oslo, 2017-2019
License: LGPL 3.0
==============================================================================*/

#ifndef THERON_ZMQ_NETWORK_LAYER
#define THERON_ZMQ_NETWORK_LAYER

#include <zmqpp.hpp>				 	                 // ZeroMQ bindings for C++

#include "Actor.hpp"                           // Theron++ Actor framework
#include "Utility/StandardFallbackHandler.hpp" // Catching unhanded messages

#include "Communication/NetworkLayer.hpp"      // Generic network layer
#include "Communication/ZMQ/ZMQMessages.hpp"   // The message format

namespace Theron::ZeroMQ
{
class NetworkLayer : virtual public Actor,
						         virtual public StandardFallbackHandler,
						         public Theron::NetworkLayer< OutsideMessage >
{
	// The server has a ZMQ context (thread) and a publisher socket for broadcast
	// of messages to remote subscribers like the other endpoints, a router
	// socket to receive messages from the others, and a map of dealer sockets,
	// one for each remote endpoint. There is a reactor polling inbound messages
	// on the subscriber and the inbound sockets. Some of these are declared as
	// static because there should be only one of these for each endpoint. This
	// is enforcing the uniqueness even though it does not make sense to run
	// multiple link server actors.

private:

	zmqpp::context ZMQContext;			 // ZMQ server thread
	zmqpp::socket  Discovery,			   // Broadcast of commands
								 Commands,         // Subscription to commands
					       Inbound;					 // Router for inbound messages
  zmqpp::reactor SocketMonitor;    // To react to incoming messages

  // The link server will cache its own external network address for sending
	// control messages.

	NetworkAddress OwnAddress;

	// Strong encapsulation is desired, and therefore this address can only be
	// read via an access function.

protected:

	inline NetworkAddress GetNetworkAddress( void ) const
	{ return OwnAddress; }

  // ---------------------------------------------------------------------------
  // Peer endpoint management
  // ---------------------------------------------------------------------------
	//
  // There is one dealer socket for each known peer in the system and these are
  // kept in a normal map. An unordered map is more efficient for the lookup,
  // but that would require hashing the addresses (longer strings) and there
  // is also a need to traverse the set of sockets beginning-to-end for which
  // a normal map is more efficient. It is therefore not clear what is the most
  // efficient container to use here.
  //
  // Implementation note: The present author had overlooked the fact that the
  // mapped value (zmqpp::socket) must be default constructable for the map
  // operator [] to work. The socket object is not, which means that using
  // the map's [] operator will produce an incomprehensible error message. The
  // solution is to use the at() function instead - in general a good advice
  // for maps since it avoids the perhaps unintended side effect that a new
  // element is inserted for an unknown key value.

private:

  std::map< NetworkAddress, zmqpp::socket > Outbound;

	// There is a utility functions to register a new peer and connect to it.

	void AddNewPeer( const NetworkAddress & PeerIP );

	// ---------------------------------------------------------------------------
	// Actor address management
	// ---------------------------------------------------------------------------
	//
  // When a local actor is registering with the session layer, its external
	// address must be resolved by the network layer. It will also be invoked
	// when a local actor wants to send a message to an unknown remote actor.
	//
	// The protocol is then that a message of "Address" type is broadcast on
	// the publisher channel subscribed to by all other endpoints in the system.
	// The request message has the "From"" field set to the address of this link,
	// and the "To" field has an empty TCP endpoint and the actor address set to
	// the address of the actor whose host is unknown.
	//
	// The response will have the "To" field set to the address of this link and
	// a fully populated "From" with the remote actor's address.

protected:

	virtual void ResolveAddress( const ResolutionRequest & TheRequest,
														   const Address TheSessionLayer ) override;

  // A remote endpoint may post a request for the address resolution for
  // an actor, and this will be forwarded by the network layer to the
  // Session Layer, and if the there is a local actor with the requested
  // name a resolution response message will be sent from the session layer
  // to the network layer.

  virtual void ResolvedAddress( const ResolutionResponse & TheResponse,
																const Address TheSessionLayer ) override;

  // When a local actor is closing it will be removed from the session layer,
  // which will then inform the network layer about the removal so that other
  // remote session layers can be informed that the actor has been removed.

  virtual void ActorRemoval( const RemoveActor & TheCommand,
														 const Address TheSessionLayer ) override;

  // ---------------------------------------------------------------------------
  // Network message handling
  // ---------------------------------------------------------------------------
	//
	// The message handler for outbound messages sent from actors on this endpoint
	// to remote actors via the session layer must also be provided overriding the
	// handler of the generic network layer class

protected:

	virtual void OutboundMessage( const OutsideMessage & TheMessage,
																const Address From ) override;

	// It is slightly more tricky to handle inbound messages because they will
	// be detected by the socket monitor which will invoke a handler function.
  // This implies, however, that the handler function is running in a thread
	// different from this Network Layer server actor's message handling thread
	// which could create race conditions on the sockets and other Network Layer
	// internal data structures. The solution is to define indicator messages
	// that will be sent form the socket monitor to the Network Layer server to
	// indicate what type of message that has arrived, and which handler that
	// should executed on this message.
	//
	// The first case to consider are messages received on the inbound socket.
	// Normal messages will be forwarded to the session layer, and protocol
	// messages when a new peer joins (see protocol described for the handler
	// in the source file) are dealt with directly.

private:

	class NewInboundMessage
	{	};

	void InboundMessage( const NewInboundMessage & TheIndicator,
											 const Address From );

	// The message handler for broadcast messages will handle subscription
	// requests by connecting to the new peer, and forward address resolution
	// commands to the the session layer.

	class NewCommandMessage
	{ };

	void CommandMessage( const NewCommandMessage & TheIndicator,
											 const Address From );

	// ---------------------------------------------------------------------------
  // Shut-down management
  // ---------------------------------------------------------------------------
	//
	// The shut down message indicates that this endpoint is closing and that
	// subscriptions to it should be closed. A final 'closing' message is sent
	// on the Discovery channel before it disconnects the Command channel from
	// all other endpoints, and disconnects all outbound sockets. Finally, the
	// sockets are closed. After this message this ZMQ endpoint will no longer
	// be able to communicate with other endpoints.

	virtual void Stop( const Network::ShutDown & StopMessage,
										 const Address Sender ) override;

  // ---------------------------------------------------------------------------
  // Constructor
  // ---------------------------------------------------------------------------
	//
	// The basic constructor takes the IP address of the first peer in the actor
	// system and the actor system name for the link server on that node. For the
	// first endpoint starting, the endpoint IP should be given as the IP address
	// of the local host (127.0.0.1). For all other IP addresses, the link will
	// send a subscribe message to the remote endpoint, see the new peer protocol
	// above documented in the network layer source file.
  //
  // If the actor system is distributed across processes on the same endpoint,
  // then Inter Process Communication (IPC) can be used instead of TCP. This
  // is indicated by setting the IP address of the first peer to an empty IP
  // address, 'IPAddress()'.
  //
  // By default is is assumed that all Network Layer servers have the same actor
	// name since no actor shall send a direct message to a link server on a
	// remote node. However, the semantic is that if the IPC is used, then the
  // the server name is used as prefix for the local IPC channels and the names
  // must be different for the two parts of the actor system to connect to
  // the other parts of the system. In this case, if the remote network layer
  // name equals the server name, it is assumed that this is the first network
  // layer server starting on this node, and it will not attempt connecting to
  // any other network layer server.
	//
	// When a Network Layer server of the distributed actor system connects not
  // as the first endpoint, the remote Network Layer server given will help
  // the new Network Layer server (endpoint) to connect to the system. The new
  // Network Layer server will then undertake the following actions:
	//
	// A. Start the sockets to listen to the given ports on the local interfaces
	// B. Subscribe to the given remote peer's publish socket
	// C. Set up an outbound socket for the remote peer, and connect this to the
  //    known peer's inbound socket.
	// D. Send a 'subscribe' message to the remote link server on its inbound
	//    request socket (connected to in step C) containing the IP address of
	//    the new node as payload
	//
	// When he remote helper Network Layer server receives the 'subscribe'
  // command it will
	//
	// 1. Publish the same 'Subscribe' message to all other peers connected to its
	//    publisher socket so that all other peers can start connecting to the
	//    new peer
	// 2. Create an outbound socket for the new remote peer
	// 3. Connect the outbound socket to the remote peer's inbound socket.
	// 4. Send a roster message to the new peer containing the IP addresses of
	//    all the other peers in the system currently known and connected.
	//
	// When a network layer server receives a 'subscribe' message on its
	// subscriber socket it will connect to the new peer's publisher socket and
	// create an outbound socket for this peer, and connect this to the new peer's
  // inbound socket.
	//
	// When the new peer receives the response from the known peer containing the
	// IP addresses of the other peers in the system, it will create outbound
	// sockets for each peer, and connect them to the  peer's inbound socket and
	// subscribe to the peer's publisher.
  //
  // When these bootstrapping steps have been completed, the new Network Layer
  // server at the endpoint is connected to the complete graph of connections
  // among all network layer servers, and may itself act as a helping peer for
  // new endpoints joining the system.

public:

	NetworkLayer( const IPAddress & InitialRemoteEndpointIP = LocalHost(),
							  const std::string & RemoteNetworkLayerServerName = "ZMQLink",
							  const std::string & ServerName = "ZMQLink" );

}; // End Link layer server class

}      // End name space Theron::ZeroMQ
#endif // THERON_ZMQ_NETWORK_LAYER
