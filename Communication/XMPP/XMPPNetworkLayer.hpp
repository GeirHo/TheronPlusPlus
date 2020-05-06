/*=============================================================================
  Network Layer

  The network layer implements the XMPP protocol [1] with the use of the
  Swiften library [2]. This library has to be built from
  scratch for most operating systems (it is packaged by the developers for
  Debian based OSes). Fedora 31: The build process is straight forward and
  also installs the header files in /usr/local/include.

  Each addressable XMPP client will have a Swiften client object as its
  interface to the link. These will be stored in a in an unordered map with
  the Jabber ID as the hashed key for fast retrieval. The network interface
  is executed in a separate thread in order to send and receive XMPP messages
  in the background.

  References:

  [1] Peter Saint-Andre, Kevin Smith, and Remko Tronçon (2009): “XMPP: The
      Definitive Guide", O’Reilly Media, Sebastopol, CA, USA,
      ISBN 978-0-596-52126-4
  [2] https://swift.im/swiften/

  Author and copyright: Geir Horn, University of Oslo, 2015 - 2020
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef THERON_XMPP_NETWORK_LAYER
#define THERON_XMPP_NETWORK_LAYER

// The standard library headers

#include <string>													// Strings
#include <memory> 												// For shared pointers
#include <thread>													// For threads
#include <unordered_map>									// Map of XMPP Clients
#include <unordered_set>									// Ditto for available remote clients
#include <functional> 										// For the hash functions
#include <stdexcept>											// For standard exceptions
#include <optional>												// For presence messages

// The Swiften XMPP library

#include "Swiften/Swiften.h"

// Headers for the Theron++ actor system

#include "Actor.hpp"                             // Theron ++ actors
#include "Utility/StandardFallbackHandler.hpp"   // Catching wrong messages
#include "Communication/NetworkLayer.hpp"        // The standard network layer
#include "Communication/XMPP/XMPPMessages.hpp"   // XMPP messages

namespace Theron::XMPP
{
class NetworkLayer
: public virtual Actor,
  public virtual StandardFallbackHandler,
	public Theron::NetworkLayer< ExternalMessage >
{
private:

  // The basic network communication consist of a network manager that captures
  // link level XMPP events, and and a thread to run the event manager in the
  // background.

  Swift::SimpleEventLoop 				EventManager;
  Swift::BoostNetworkFactories	NetworkManager;
  std::thread 		 							CommunicationLink;

  // The link needs to remember its own Jabber ID, as the external addresses
  // of the local actors will be based on this ID.

  JabberID ProtocolID;

	// The roster is returned as a set of JabberIDs

  using Roster = std::set< JabberID >	;

  // In order to register with the local server, a password is needed

  std::string ServerPassword;

  // ---------------------------------------------------------------------------
  // ACTOR CLIENT MANAGEMENT
  // ---------------------------------------------------------------------------
  //
  // Pointers to the active clients are stored in an unordered map where the
  // key is the string version of the Jabber ID

  using ClientObjectPointer = std::shared_ptr< Swift::Client >;

  // The priority of a client must be stored so that it can be attached to
  // the presence messages the client will send:

  using PrecencePriority= std::optional< int > ;

  // Two information items must be kept for each client: The pointer to
  // the client object, and the client's priority. Furthermore, the class must
  // provide both a move constructor and a copy constructor since it will be
  // used as the value field of standard container.
  //
  // A complicating factor is the XMPP mutual presence and availability. A
  // local actor cannot send messages to a remote actor before that actor is
  // available. As this is an actor-to-actor relationship the same remote
  // actor can already be available to other actors on this network endpoint,
  // and therefore known as a valid address at the session layer. Yet, a newly
  // created actor may have to await the full handshake to be completed before
  // messages can be sent. If the handshake has not been completed, messages
  // must be queued, waiting for the remote actors availability.
  //
  // Fundamentally, we would need a three dimensional structure where the
  // first two dimensions would be the pair of peers, and the third an
  // availability flag or a queue of pending messages. This is implemented
  // here with two structures: One set to store a client's active remote peers,
  // and one multi-map of stored messages sorted on the remote peer address.
  // These fields are private and can only be accessed through the appropriate
  // supporting methods.

  class ClientRecord
  {
  public:

    ClientObjectPointer TheClient;
    PrecencePriority    Priority;

  private:

    std::unordered_set< JabberID >                       ActivePeers;
    std::unordered_multimap< JabberID, ExternalMessage > MessageQueue;

  public:

    // The new peer method register a remote peer in the set of active peers
    // and checks if there are any queued messages for this peer, and if so
    // they will be sent and removed from the queue.

    void NewPeer( const JabberID & ThePeer );

    // Similarly, there is a method to remove a peer from the set of active
    // peers when it becomes unavailable.

    void RemovePeer( const JabberID & ThePeer );

    // Finally there is a method to send the messages from this client. It
    // will check if the client's address equals the sender address in the
    // message. If they do not match an error will be thrown. However, if the
    // remote peer is active, the message will be sent, otherwise the message
    // will be queued.

    void SendMessage( const ExternalMessage & Message );

    // Constructors for using this structure with standard library containers

    ClientRecord ( ClientObjectPointer TheNewClient,
		   PrecencePriority    AssignedPriority = PrecencePriority() )
    : TheClient( TheNewClient ), Priority(  AssignedPriority ),
      ActivePeers(), MessageQueue()
    { }


    ClientRecord( const ClientRecord && OtherRecord )
    : TheClient( OtherRecord.TheClient ),
      Priority( OtherRecord.Priority ),
      ActivePeers( OtherRecord.ActivePeers ),
      MessageQueue( OtherRecord.MessageQueue )
      { }

    ClientRecord( const ClientRecord & OtherRecord )
    : TheClient( OtherRecord.TheClient ),
      Priority( OtherRecord.Priority ),
      ActivePeers( OtherRecord.ActivePeers ),
      MessageQueue( OtherRecord.MessageQueue )
      { }
  };

  // An unordered map is used to look up the client record for a given client
  // address. The Jabber IDs are used as lookup.

  std::unordered_map< JabberID, ClientRecord > Clients;

protected:

  // There are also interfaces to the New Peer and Remove Peer functions that
  // looks up a peer by its address and call the corresponding function on
  // that peer (if found) or else it will throw an invalid argument exception
  // if the given local Jabber ID is unknown.

  void NewPeer   ( const JabberID & LocalID, const JabberID & RemoteID );
  void RemovePeer( const JabberID & LocalID, const JabberID & RemoteID );

  // When a client is created it is first registered with the XMPP server
  // and when this process is complete, the client registered event is
  // triggered, that in turn will ask the client to connect to the server.

  virtual void ClientRegistered( JabberID 		            ClientID,
																 Swift::Payload::ref      RegistrationResponse,
																 Swift::ErrorPayload::ref Error  );

  // XMPP client configuration and feedback are sort of hard coded for this
  // extension since it is assumed that the behaviour of each client will be
  // more or less the same. When a client is connected it will request it its
  // roster, and when the roster is received a presence message is sent to all
  // subscribers. The big issue is of course how remote clients will detect
  // that an actor's client becomes on-line and then subscribe to it. Derived
  // classes could re-implement this functionality.

  virtual void ClientConnected( JabberID ClientID );

  virtual void InitialRoster( JabberID ClientID,
												      Swift::RosterPayload::ref TheRoster,
												      Swift::ErrorPayload::ref  Error      );

  // Presence handling is more elaborate as some presence information require
  // a double response, some a single response, and some no response at all.

  virtual void PresenceNotification( JabberID ClientID,
																     Swift::Presence::ref PresenceReceived );

  // There is a simple function to create and send a single presence message.
  // Note that the destination address can be omitted for broadcast presence
  // messages.

  virtual void SendPresence( JabberID FromClient,
												     Swift::Presence::Type PresenceType,
												     JabberID ToClient = JabberID() );

  // The roster for a client can be requested by another actor at any time
  // and there is therefore a special handler that gets the roster and
  // dispatches the IDs of each connected peer back to the requesting actor.
  // the returned roster is a set of Jabber IDs.

  virtual void DispatchRoster( const Address WaitingActor,
												       Swift::RosterPayload::ref TheRoster,
												       Swift::ErrorPayload::ref  Error      );

  // There are also options to be set for the clients. These are generally
  // set in the constructor, and will be used when creating the clients.

  Swift::ClientOptions ClientsOptions;

private:

  // ---------------------------------------------------------------------------
  // ACTOR DISCOVERY
  // ---------------------------------------------------------------------------
  //
  // In a pure peer-to-peer system a joining network endpoint needs to know
  // about other network endpoints. The XMPP protocol offers us the opportunity
  // to go for an "any peer" approach: A new endpoint will only need one other
  // endpoint to connect to as follows:
  //
  // 	1) When the link starts it connects to the XMPP server at domain, with
  //     the resource "endpoint" (e.g. as "EndPointName@domain/endpoint")
  // 	2) It then sends a presence to the initial peer it has been told about
  //  3) Both peers undergo the subscribe-subscribed-available handshake
  // 	4) When the original peer receives the "available" message from another
  //	   peer with the "endpoint" resource it requests its own roster
  //	5) When it gets the roster it dispatches the roster to the new peer
  //	   as a sequence of messages with subject "SUBSCRIBE" and payload the
  //	   Jabber ID of each endpoint known to the original peer
  // 	6) When the joining endpoint gets these messages, it will "subscribe"
  //	   to the remote agents (= initiating the handshake and thereby adding
  //	   them to its own roster making it ready to serve new endpoints
  //	   joining the system).
  //
  // The first two steps are being done in the XMPP Network Layer's constructor,
	// and the standard Presence Notification handler will deal with the handshake
  // necessary in step 3) and it will initiate the rooster request. There is
  // a dedicated function to request the roster that takes a callback function
  // to invoke when the roster is returned from the XMPP server.
  //
  // The roster request is a function used whenever the roster should be
  // checked: when a client is connected for the first time, when an external
  // actor asks for this roster, or when an existing network endpoint detects
  // that a new endpoint indicates that it is "available". It basically check
  // that the local client ID is valid, and then use this client to construct
  // the request that will call the callback function when the roster is
  // received.

  template< typename LambdaFunction >
  void RosterRequest( JabberID ClientID, LambdaFunction CallBack )
  {
    auto TheClientRecord = Clients.find( ClientID );

    if ( TheClientRecord != Clients.end() )
    {
      ClientObjectPointer TheClient( TheClientRecord->second.TheClient );

      Swift::GetRosterRequest::ref Request = Swift::GetRosterRequest::create(
	    TheClient->getIQRouter()
      );

      Request->onResponse.connect( CallBack );
      Request->send();
    }
  }

  // The callback function used with the roster request will be implementing
  // step 5 of the actor discovery and send the subscribe messages.

  void DispatchKnownPeers( JabberID ThisEndpoint, JabberID NewEndpoint,
												   Swift::RosterPayload::ref TheRoster,
												   Swift::ErrorPayload::ref  Error      );

  // The received messages will be captured by the new endpoint and it will
  // immediately subscribe to the given Jabber ID if the subject of the
  // received message is "SUBSCRIBE". For all other messages it will throw an
  // invalid argument exception.

  void SubscribeKnownPeers( JabberID NewEndpoint,
												    Swift::Message::ref XMPPMessage );

  // There is a special presence handler handling first time availability. This
  // will ignore presence messages from resources on the local address, and
  // if it gets a subscription from another endpoint, it will send its roster
  // to that endpoint.

  void EndpointPresence( JabberID ClientID,
												 Swift::Presence::ref PresenceReceived );

	// ---------------------------------------------------------------------------
  // Adding and removing actors
  // ---------------------------------------------------------------------------
  //
	// When the a new actor is created the session layer will send a resolution
	// request for its address. It will then create the client for the actor,
	// connect the actor to the server and return its external address.
	//
	// The implied XMPP protocol sends a presence message for this actor to all
  // other endpoints, and the present notification handler will then send an
  // address resolved message to the Session Layer. Hence, all remote actors
  // that have been registered should have their addresses already cached by
  // the session layer. A special protocol to detect messages on remote
  // endpoints is therefore not needed, and the below resolution request should
  // never be used by the Session Layer for a remote actor (unless something
  // is wrong and a local actor tries to send to a remote actor that has not
  // correctly registered its presence). The resolve address will therefore
	// ignore the request for a non-local actor, and then the session layer has
	// to cache the messages awaiting a future availability presence for the actor
	// when it eventually becomes available on a remote node.

protected:

	virtual void ResolveAddress( const ResolutionRequest & TheRequest,
														   const Address TheSessionLayer ) override;

  // The normal protocol when receiving a request from a remote node to resolve
  // and actor address would be to send a resolution request to the session
  // layer asking if this actor is known on this endpoint. The session layer
  // should then confirm with a resolution response only if the actor is known,
  // and the network layer should then implement the necessary actions to
  // inform all or a selected set of remote endpoints about the availability
  // of this local actor on this endpoint. Now, since the remote endpoints
  // have already been informed about the local actor when the actor was
  // resolving its address using the previous Resolve Address function, they
  // should never ask for a new resolution, and the network layer should never
  // ask the session layer, and the session layer should never generate a
  // response. Consequently, the required response handler has nothing to
  // do. The handler must be defined, as it is registered as a message handler
  // by the generic network layer base class, but it will only throw a logic
  // error exception.

  virtual void ResolvedAddress( const ResolutionResponse & TheResponse,
																const Address TheSessionLayer ) override;

  // When local actors are removed the session layer will send a request to
  // delete the actor, and this will tell all other endpoints that the client
  // is unavailable, and then disconnect the client.

  virtual void ActorRemoval( const RemoveActor & TheCommand,
														 const Address TheSessionLayer ) override;
  // ---------------------------------------------------------------------------
  // Actor to actor communication
  // ---------------------------------------------------------------------------
  //
  // Normal messages will arrive from the Session Layer in the form of
  // the above Outside Message class. These messages will be captured by
  // the Outbound Message handler of the Network Layer.

  virtual void OutboundMessage( const ExternalMessage & TheMessage,
																const Address From ) override;

	// ---------------------------------------------------------------------------
  // Shut-down management
  // ---------------------------------------------------------------------------
	//
	// When the shut down message arrives, all local actors shall have asked to
	// be removed, and the network layer must just inform its peers that this
	// endpoint is shutting down and then close the connection. Hence, it will
	// shut down the client associated with this endpoint and delete all clients.

	virtual void Stop( const Network::ShutDown & StopMessage,
										 const Address Sender ) override;

public:

  // ---------------------------------------------------------------------------
  // CONSTRUCTOR & DESTRUCTOR
  // ---------------------------------------------------------------------------

  NetworkLayer( const std::string & EndpointName,
								const std::string & EndpointDomain,
				        const std::string & TheServerPassword,
				        const JabberID & InitialRemoteEndpoint = JabberID(),
				        const std::string & ServerName = "XMPPNetworkLayer"  );

  virtual ~NetworkLayer();
};

}      // End name space Theron XMPP
#endif // THERON_XMPP_NETWORK_LAYER
