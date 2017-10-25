/*=============================================================================
  XMPP - Extensible Messaging and Presence Protocol 
  
  This header defines the classes necessary to implement the XMPP [1] support 
  for Theron building on the transparent communication framework consisting of 
  the following hierarchy:
  
  (Physical link) <-> Network Layer <-> External message <-> Session Layer 
  <-> Presentation Layer::Message <-> Presentation Layer 
  <-> (User implemented actor)
  
  The actual link level XMPP transmission will be ensured by the Swiften 
  library [2] and it is used in the Link Server that adds a separate thread to
  support the communication. 
  
  The Session Layer generates the external Jabber IDs for the local actors 
  and register XMPP clients for these with the Link Server. The roster of the 
  XMPP client is queried when a local actor wants to send to a remote actor 
  whose Jabber ID is not already known by the protocol engine.
  
  The Presentation Layer is application dependent and not XMPP dependent, and so 
  it is not included in this implementation. 
  
  ACKNOWLEDGEMENT:
  
  The author is grateful to Dr. Salvatore Venticinque of the Second University
  of Naples for the selection of the Swiften library, and for guiding 
  implementations of similar mechanism for the CoSSMic project that has been 
  the basis for the Network Layer implementation here. Without his kind help 
  this XMPP interface would never have happened.
  
  REFERENCES:
  
  [1] Peter Saint-Andre, Kevin Smith, and Remko Tronçon (2009): “XMPP: The 
      Definitive Guide", O’Reilly Media, Sebastopol, CA, USA, 
      ISBN 978-0-596-52126-4
  [2] https://swift.im/swiften/
  
  Author: Geir Horn, University of Oslo, 2015 - 2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef THERON_XMPP_COMMUNICATION
#define THERON_XMPP_COMMUNICATION

// The standard library headers

#include <string>													// Strings
#include <memory> 												// For shared pointers
#include <thread>													// For threads
#include <unordered_map>									// Map of XMPP Clients
#include <unordered_set>									// Ditto for available remote clients
#include <functional> 										// For the hash functions
#include <stdexcept>											// For standard exceptions
#include <optional>												// For presence messages

// Generic frameworks - Theron for actors and Swiften for XMPP

#include "Actor.hpp"   										// Theron++ Actors
#include "StandardFallbackHandler.hpp"

// The generic frameworks used  - it should be noted that Swiften uses the 
// Boost::signals. This is depreciated and replaced with Boost::signals2, but 
// the old version still works though, so we just disable the compiler warnings

#define BOOST_SIGNALS_NO_DEPRECATION_WARNING

// Another serious problem with Swiften is that it uses Boost::shared_ptr and 
// in many of the functions used to check the availability of some information
// this pointer is converted to a boolean. Implicitly it is tested for being 
// non-null, but it is correctly illegal to say
//    bool TestValue = Boost::shared_ptr<T>();  
// as no converter exists from a Boost::shared_ptr to bool. The boost library
// is well written, and it uses "explicit" as a way to prevent unintentional 
// use. However, the Swiften library uses only implicit conversions, and 
// we must therefore indicate to boost that this type of conversions should 
// be allowed by switching off the "explicit" conversions

#define BOOST_NO_CXX11_EXPLICIT_CONVERSION_OPERATORS

// There are also a couple of other obscure errors making the following 
// boost headers to be needed BERFORE Swiften can be loaded. This should 
// have been done by Swiften itself, but apparently it is not.

#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>

// Then the Swiften library can be safely loaded.

#include <Swiften/Swiften.h>	// XMPP link level protocol

// In order to use the Swiften Jabber ID as the key in unordered maps, a hash 
// function must be provided taking the Jabber ID as the argument to the () 
// operator. It is implemented as a template specification for the Jabber ID 
// that uses the standard hash function on the string version of the Jabber 
// ID. It is defined as part of the standard names pace to avoid that it has 
// to be explicitly given to the unordered map since the compiler will find 
// an object std::hash< Swift::JID>. Note that the return statement calls 
// the hash operator on a temporary string hash object (hence the many 
// parentheses)

namespace std {
  template<>
  class hash< Swift::JID >
  {
  public:
    
    size_t operator() ( const Swift::JID & TheID ) const
    {
      return std::hash< string >()( TheID.toString() );
    }
  };
}

// The same extension must be done for the boost hash function used by 
// boost::bimap. Since it is identical to the standard one above, it should 
// be possible to reuse that code, but currently I have not found a way to 
// do it.

namespace boost {
  template<>
  class hash< Swift::JID >
  {
  public: 
    size_t operator() ( const Swift::JID & TheID ) const
    {
      return std::hash< std::string >()( TheID.toString() );
    }
  };
}

// Including the transparent communication framework

#include "NetworkEndPoint.hpp"
#include "LinkMessage.hpp"
#include "NetworkLayer.hpp"
#include "SessionLayer.hpp"

// These classes belongs to the Theron transparent communication extensions 
// and the XMPP sub name space.

namespace Theron::XMPP 
{
// ---------------------------------------------------------------------------
// Jabber ID = external address
// ---------------------------------------------------------------------------
//
// The external address in XMPP is the Jabber ID which is simply a re-naming 
// of the ID class provided by the Swiften library.
// JID("node@domain/resource") == JID("node", "domain", "resource")
// JID("node@domain") == JID("node", "domain")
  
using JabberID = Swift::JID;

// ---------------------------------------------------------------------------
// Link Message
// ---------------------------------------------------------------------------
// 
// Given that the Network Layer implements the link level XMPP protocol, the link 
// message is not used on the link, but only to send a message with a payload 
// between the link server and the protocol engine.

class OutsideMessage : public Theron::LinkMessage< JabberID >
{
private:
  
  // The data fields store the "to" and "from" address and the payload. The 
  // subject field can be used if the message needs one or is a command 
  // between low XMPP layers.
  
  JabberID               SenderAddress, ReceiverAddress;
  SerialMessage::Payload Payload;
	std::string            Subject;
  
public:
  
  // There is a default constructor that just initialises the message.
  
  OutsideMessage( void )
  : SenderAddress(), ReceiverAddress(), Payload(), Subject()
  { };
  
  // Then there is a message dealing with all the required information at 
  // once. 
  
  OutsideMessage( const JabberID & From, const JabberID & To ,
								  const std::string & ThePayload, 
								  const std::string TheSubject = std::string()  )
  : SenderAddress( From ), ReceiverAddress( To ), 
    Payload( ThePayload ), Subject( TheSubject )
  { };
  
  // A copy constructor is necessary to queue messages 
  
  OutsideMessage( const OutsideMessage & TheMessage )
  : SenderAddress  ( TheMessage.SenderAddress ),
    ReceiverAddress( TheMessage.ReceiverAddress ),
    Payload	   		 ( TheMessage.Payload ),
    Subject        ( TheMessage.Subject )
  { };
  
  // The virtual interface functions for setting the various fields
  
  virtual void SetPayload( const SerialMessage::Payload & ThePayload ) override
  { Payload = ThePayload; };
  
  virtual void SetSender( const JabberID & From ) override
  { SenderAddress = From; };
  
  virtual void SetRecipient( const JabberID & To   ) override
  { ReceiverAddress = To; };
  
  virtual void SetSubject( const std::string & TheSubject )
  { Subject = TheSubject; }

  // There are similar functions to obtain or set the sender and the receiver of 
  // this message.
      
  virtual SerialMessage::Payload GetPayload( void ) const override
  { return Payload; };
  
  virtual JabberID GetRecipient( void ) const override
  { return ReceiverAddress; };
  
  virtual JabberID GetSender( void ) const override
  { return SenderAddress; };
  
  virtual std::string GetSubject( void ) const
  { return Subject; }
  
	virtual ~OutsideMessage( void )
	{ }
};
  
/*=============================================================================
//
// Link Server
//
=============================================================================*/
//
// Each addressable XMPP client will have a Swiften client object as its 
// interface to the link. These will be stored in a in an unordered map with 
// the Jabber ID as the hashed key for fast retrieval. The network interface 
// is executed in a separate thread in order to send and receive XMPP messages
// in the background. 
// 
// As an actor this class receives commands from the protocol engine containing
// a command and a Jabber ID for which some actions should be executed.
//
// Not quite documented is the fact that the XMPP client connects to the XMPP
// server running at the domain of the connecting client. Thus, if the 
// connect request has ha Jabber ID of the form "me@localhost" then there must
// be an XMPP server running at "localhost", otherwise the connection will 
// tacitly fail and not be connected. 

class Link : public virtual Actor,
						 public virtual StandardFallbackHandler,
						 public NetworkLayer< OutsideMessage >
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

  // Pointers to the active clients are stored in an unordered map where the 
  // key is the string version of the Jabber ID
  
  using ClientObjectPointer = std::shared_ptr< Swift::Client >;
  
  // The priority of a client must be stored so that it can be attached to 
  // the presence messages the client will send:
  
  using PrecencePriority= std::optional<int> ;
  
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
    
    std::unordered_set< JabberID > 			                ActivePeers;
    std::unordered_multimap< JabberID, OutsideMessage > MessageQueue;
    
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
    
    void SendMessage( const OutsideMessage & Message );
    
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
  
  // XMPP client configuration and feedback sort of hard coded for this 
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

  // In a pure peer-to-peer system a joining network endpoint needs to know 
  // about other network endpoints. The XMPP protocol offers us the opportunity
  // to go for an "any peer" approach: A new endpoint will only need one other
  // endpoint to connect to as follows:
  //
  // 	1) When the link starts it connects to the XMPP server at domain, with 
  //       the resource "endpoint" (e.g. as "EndPointName@domain/endpoint")
  // 	2) It then sends a presence to the initial peer it has been told about
  //  	3) Both peers undergo the subscribe-subscribed-available handshake
  // 	4) When the original peer receives the "available" message from another
  //	   peer with the "endpoint" resource it requests its own roster
  //	5) When it gets the roster it dispatches the roster to the new peer 
  //	   as a sequence of messages with subject "SUBSCRIBE" and payload the 
  //	   JabberID of each endpoint known to the original peer
  // 	6) When the joining endpoint gets these messages, it will "subscribe" 
  //	   to the remote agents (= initiating the handshake and thereby adding 
  //	   them to its own roster making it ready to serve new endpoints 
  //	   joining the system).
  //
  // The first two steps are being done in the XMPP Link's constructor, and 
  // the standard Presence Notification handler will deal with the handshake 
  // necessary in step 3) and it will initiate the rooster request. There is 
  // a dedicated function to request the roster that takes a callback function
  // to invoke when the roster is returned from the XMPP server.
  
  // The roster request is a function used whenever the roster should be 
  // checked: when a client is connected for the first time, when an external 
  // actor asks for this roster, or when an existing network endpoint detects 
  // that a new endpoint indicates that it is "available". It basically check  
  // that the local client ID is valid, and then use this client to construct 
  // the request that will call the callback function when the roster is 
  // received.
  //
  // Since this is intended to be used with lambda functions, the signature 
  // generally becomes nasty. In theory it should be possible to pass the lambda
  // as a standard function, but since the function arguments are based on 
  // Swift type definitions, these definitions are expanded in the signature but 
  // not in the lambda argument list when it is defined and the compiler 
  // complains that the conversion cannot be made. The function can more 
  // readably be implemented as a template, achieving the desired behaviour.

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
  
  void DispatchKnownPeers( JabberID ThisEndpoint, JabberID NewEnbpoint, 
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
	// connect the actor to the server and return its address.

protected:
		
	virtual void ResolveAddress( const ResolutionRequest & TheRequest, 
														   const Address TheSessionLayer ) override;
															 
  // When local actors are removed the session layer will send a request to 
  // delete the actor, and this will tell all other endpoints that the client 
  // is unavailable, and then disconnect the client.
															 
  virtual void ActorRemoval( const RemoveActor & TheCommand, 
														 const Address TheSessionLayer ) override;
  // ---------------------------------------------------------------------------
  // NORMAL MESSAGES ==> XMPP::Link
  // ---------------------------------------------------------------------------
  
  // Normal messages will arrive from the Session Layer in the form of 
  // the above Outside Message class. These messages will be captured by 
  // the Outbound Message handler of the Network Layer.
	
  virtual void OutboundMessage( const OutsideMessage & TheMessage, 
																const Address From ) override;
	  
public:

  // ---------------------------------------------------------------------------
  // CONSTRUCTOR & DESTRUCTOR
  // ---------------------------------------------------------------------------
  
  Link( const std::string & EndpointName, const std::string & EnpointDomain, 
				const std::string & ServerPassword,
				const JabberID & InitialRemoteEndpoint = JabberID(),
				const std::string & ServerName = "XMPPLink"  );
  
  virtual ~Link();
    
}; // End - XMPP Link Server

/*=============================================================================

 XMPP Session layer

=============================================================================*/
//
// The session layer must provide a default encoding of the external address 
// of an actor based on its actor address. 

using SessionLayer = Theron::SessionLayer< OutsideMessage >;

/*=============================================================================

 XMPP Presentation layer

=============================================================================*/
//
// The presentation layer is simply reused as it is.

using PresentationLayer = Theron::PresentationLayer;

/*=============================================================================

 XMPP Manager (Network Endpoint)

=============================================================================*/

// The XMPP Manager is responsible for creating the classes needed 
// for the communication. Basically it is only the constructor that needs 
// implementation to initiate the XMPP classes.

class Network : virtual public Actor,
								public Theron::Network
{
protected:
	
	// The server password and the JabberID are read-only fields for derived 
	// classes and can only be set upon construction
	
  const std::string ServerPassword;
  const JabberID    InitialRemoteEndpoint;
	
	// The virtual functions to create the actors are implemented in the source
	// file for the XMPP link
	
  virtual void CreateNetworkLayer( void ) override;
	virtual void CreateSessionLayer( void ) override;
	virtual void CreatePresentationLayer( void ) override;

  // The constructor is only invoking the end point constructor and forward  
  // the Initialiser object to the base class. Note that the initialiser must
  // be set by the Network End Point's Set Initialiser function when this 
  // constructor is called.
	
  Network( const std::string & Name, const std::string & Location, 
					 const std::string & Password,
					 const JabberID & AnotherPeer = JabberID() )
  : Actor( Name ),
    Theron::Network( Location ),
    ServerPassword( Password ), InitialRemoteEndpoint( AnotherPeer )
  { }
  
  // The virtual destructor is currently not doing anything

public:
  
  virtual ~Network(void)
  { }
  
}; 		 // End XMPP::NetworkEndpoint

}      // End namespace Theron::XMPP
#endif // THERON_XMPP_COMMUNICATION
