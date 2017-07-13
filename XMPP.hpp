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
  
  Author: Geir Horn, University of Oslo, 2015, 2016
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

// The optional struct will be used for the priority of a presence message
// to ensure that it can be used even if there is no value value for it

#include <boost/optional.hpp>

// Then the Swiften library can be safely loaded.

#include <Swiften/Swiften.h>	// XMPP link level protocol

// In order to use the Swiften Jabber ID as the key in unordered maps, a hash 
// function must be provided taking the Jabber ID as the argument to the () 
// operator. It is implemented as a template specification for the Jabber ID 
// that uses the standard hash function on the string version of the Jabber 
// ID. It is defined as part of the std namespace to avoid that it has to be 
// explicitly given to the unordered map since the compiler will find an object
// std::hash< Swift::JID>. Note that the return statement calls the hash 
// operator on a temporary string hash object (hence the many parentheses)

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
// boost::bimap.

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
namespace Theron {
// and each protocol type should have its own name space to make it explicit 
// which protocol is in use
namespace XMPP {

// ---------------------------------------------------------------------------
// Jabber ID = external address
// ---------------------------------------------------------------------------
//
// The external address in XMPP is the Jabber ID which is simply a re-naming 
// of the ID class provided by the Swiften library.
// JID("node@domain/resource") == JID("node", "domain", "resource")
// JID("node@domain") == JID("node", "domain")
  
typedef Swift::JID JabberID;

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
  
  JabberID    SenderAddress, ReceiverAddress;
  std::string Payload, Subject;
  
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
    Payload	   ( TheMessage.Payload ),
    Subject        ( TheMessage.Subject )
  { };
  
  // The virtual interface functions for setting the various fields
  
  virtual void SetPayload( const std::string & ThePayload )
  {
    Payload = ThePayload;
  };
  
  virtual void SetSender( const JabberID & From )
  {
    SenderAddress = From;
  };
  
  virtual void SetRecipient( const JabberID & To   )
  {
    ReceiverAddress = To;
  };
  
  virtual void SetSubject( const std::string & TheSubject )
  {
    Subject = TheSubject;
  }

  // There are similar functions to obtain or set the sender and the receiver of 
  // this message.
      
  virtual std::string GetPayload( void ) const
  {
    return Payload;
  };
  
  virtual JabberID GetRecipient( void ) const
  {
    return ReceiverAddress;
  };
  
  virtual JabberID GetSender( void ) const 
  {
    return SenderAddress;
  };
  
  virtual std::string GetSubject( void ) const
  {
    return Subject;
  }
  
  // The operator () is a short cut to the full constructor performing all 
  // the above set operations in one go. The format of this is chosen to 
  // mirror the Theron framework's send function and it does not support 
  // the subject for this reason.
 
  virtual void operator() ( const std::string  & ThePayload, 
												    const JabberID     & From,
												    const JabberID     & To )
  {
    Payload = ThePayload;
    SenderAddress = From;
    ReceiverAddress = To;
  };

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

  // ---------------------------------------------------------------------------
  // ACTOR CLIENT MANAGEMENT
  // ---------------------------------------------------------------------------

  // Pointers to the active clients are stored in an unordered map where the 
  // key is the string version of the Jabber ID
  
  typedef std::shared_ptr< Swift::Client > ClientObjectPointer;
  
  // The priority of a client must be stored so that it can be attached to 
  // the presence messages the client will send:
  
  typedef boost::optional<int> PrecencePriority;
  
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
  // and one multimap of stored messages sorted on the remote peer address.
  // These fields are private and can only be accessed through the appropriate
  // supporting methods.

  class ClientRecord
  {
  public:
    
    ClientObjectPointer TheClient;
    PrecencePriority    Priority;
    
  private:
    
    std::unordered_set< JabberID > 			ActivePeers;
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
  
  virtual void ClientRegistered( JabberID 		  ClientID, 
																 Swift::Payload::ref      RegistrationResponse,
																 Swift::ErrorPayload::ref Error  );
  
  // XMPP client configuration and feedback sort of hard coded for this 
  // extension since it is assumed that the behaviour of each client will be 
  // more or less the same. When a client is connected it will request it its 
  // roster, and when the roster is received a presence message is sent to all 
  // subscribers. The big issue is of course how remote clients will detect 
  // that an actor's client becomes on-line and then subscribe to it. Derived
  // classes could re-implement this functionality.

  virtual void ClientConnected( JabberID  ClientID  );
  
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
  
public:

  // ---------------------------------------------------------------------------
  // CONSTRUCTOR & DESTRUCTOR
  // ---------------------------------------------------------------------------
  
  Link( NetworkEndPoint * Host, std::string ServerPassword,
				const JabberID & InitialRemoteEndpoint = JabberID(),
				std::string ServerName = "XMPPLink"  );
  
  virtual ~Link();

  // ---------------------------------------------------------------------------
  // COMMANDS & HANDLERS
  // ---------------------------------------------------------------------------
  
  // The protocol engine may ask for new clients to be created as local 
  // actors demands an external presence, and subsequently delete these 
  // clients if the local actor no longer needs this communication channel. 
  // It is also possible for a client to request the roster to see which 
  // other remote actors that are available for communication. Each of these
  // take and hold the Jabber ID of the local actor.
  
  class Command
  {
  private:
    
    JabberID ClientID;
    
  public:
    
    inline JabberID GetJID( void ) const
    {
      return ClientID;
    }
    
    Command( const JabberID & TheID )
    : ClientID( TheID )
    { };
  };
  
  // NEW CLIENT ( ===> XMPP::Link )
  //
  // The New Client command instructs the link server to create a new actor
  // client when the handler for this command receives a request from the 
  // protocol engine. Apparently a new client must first register with the 
  // XMPP server, and then connect. Thus the request from the new actor is 
  // sent to the Create Client handler, that creates the client struct and 
  // register the client. The callback from the registration process will 
  // trigger the connect actions for the client.
  
  class NewClient : public Command
  {
  private: 
    
    std::string      Password;
    PrecencePriority Priority;
    
  public:
    
    NewClient(const JabberID & TheID, std::string ThePassword, 
	      PrecencePriority ThePriority = PrecencePriority() ) 
    : Command( TheID ), Password( ThePassword ), Priority( ThePriority )
    { };
    
    std::string GetPassword( void ) const
    {
      return Password;
    }
    
    PrecencePriority GetPriority( void ) const
    {
      return Priority;
    }
  };
  
  void CreateClient( const NewClient & Request, const Address From );
  
  // DELETE CLIENT ( ===> XMPP::Link )
  //
  // This command class and handler has the opposite effect: If there is a 
  // client class for this ID it will be destroyed, otherwise nothing will
  // happen
  
  class DeleteClient : public Command
  {
  public:
    
    DeleteClient( const JabberID & TheID ) : Command( TheID )
    { };
  };
  
  void DestroyClient( const DeleteClient & Request, const Address From );
  
  // GET ROSTER ( ==> XMPP::Link )
  //
  // This command is used when a local actor wants to identify which remote 
  // actors are available for communication. The handler simply returns the 
  // available Jabber IDs back to the caller actor. This is done in two parts:
  // a message handler receiving the request for the roster and passing this 
  // request to the XMPP server, and then register the dispatch roster 
  // handler which will do the dispatching when the roster arrives.
  
  class GetRoster : public Command
  {
  public:
    
    GetRoster( const JabberID & TheID ) : Command( TheID )
    { };
  };
  
  // The roster is returned as a set of JabberIDs
  
  typedef std::set< JabberID >	Roster;
  
  // A local actor can request its rooster by sending the Get Rooster command
  // to the link, and this will be managed by the next handler.
  
  void RequestRoster( const GetRoster & Request, const Address From );
  
  // AVAILABILITY MESSAGES ( ==> Session Layer )
  //
  // Availability and unavailability of clients will be sent to the Session
  // Layer server as they arrive, using the availability status class as the 
  // information carrier. The Session Layer must implement a handler 
  // to receive this message type.
  
  class AvailabilityStatus : public Command
  {
  public: 
    
    enum class StatusType
    {
      Available, Unavailable
    };
    
  private:
    
    StatusType Status;
    
  public:
    
    AvailabilityStatus( const JabberID & TheRemoteID, StatusType CurrentStatus )
    : Command( TheRemoteID ), Status( CurrentStatus )
    { };
    
    inline StatusType GetStatus( void ) const
    {
      return Status;
    }
    
  };

  // ---------------------------------------------------------------------------
  // NORMAL MESSAGES ==> XMPP::Link
  // ---------------------------------------------------------------------------
  
  // Normal messages will arrive from the Session Layer in the form of 
  // the above Outside Message class. These messages will be captured by 
  // the Outbound Message handler of the Network Layer which will then 
  // call the SendMessage function in order to implement the link specific 
  // actions.
  
  virtual void SendMessage( const OutsideMessage & TheMessage );
    
}; // End - XMPP Link Server

/*=============================================================================
//
// XMPP Protocol Engine
//
=============================================================================*/
// 
// The XMPP Protocol Engine will connect as an XMPP client to a given domain 
// server, and then wait for local actors to register with the protocol 
// engine. The local actors will be registered as resources for the protocol 
// engine actor. 
// 
// A special address encoding scheme is used for the Theron XMPP protocol. In 
// general an XMPP address is at the form node@domain/resource where the 
// user has an account on a local XMPP server. The idea is that the user name is 
// an identifier of the Theron actor system and that all local actors are 
// "resources" of this actor system. The endpoint name is typically the node
// part of of this address. Hence, everything up to the resource string will 
// be the same for all actors on this endpoint. Every local actor that wants 
// external visibility must register as an actor, and will then be assigned 
// the correct Jabber ID.
//
// Furthermore, when the local actor is registered in the Theron XMPP Link 
// server, it will also be signed in to a Multi-User Chat (MUC) room to 
// broadcast its availability. When the other actors in the MUC sees this 
// sign-in they will send an XMPP Presence message to the newly signed-in actor.
// These messages will automatically be acknowledged with presence messages 
// back to the sending actors. In this way, all actors will know about all 
// actors.
//
// When an XMPP Presence message is received by the XMPP Link Server it will 
// send a message to the Store Remote Address handler on the session layer 
// which will register the remote address. This implies that remote addresses
// are being pushed to the session layer, and address resolution becomes a 
// different concept. 

class XMPPProtocolEngine 
: public virtual Actor, 
  public virtual StandardFallbackHandler,
  public virtual SessionLayer< OutsideMessage >
{
  // --------------------------------------------------------------------------
  // Address management
  // --------------------------------------------------------------------------
  
private:

  // The engine needs to remember its own Jabber ID, as the external addresses
  // of the local actors will be based on this ID.
  
  JabberID ProtocolID;
  
  // In order to register with the local server, a password is needed
  
  std::string ServerPassword;
  
protected:
  
  // The actor register function must be defined since the standard Session  
  // Layer does not know how to map a local actor address to a well defined 
  // Jabber ID and the need to create a client for this actor in the XMPP 
  // Network Layer.
  
  virtual void RegisterActor ( 
	       const SessionLayerMessages::RegisterActorCommand & Command, 
	       const Address LocalActor );

  // The standard functionality for removing actors must be augmented with 
  // a command to the XMPP Link to remove the XMPP client associated with 
  // this local actor, and the handler must therefore be overloaded.
  
  virtual void RemoveActor ( 
	       const SessionLayerMessages::RemoveActorCommand & Command, 
	       const Address LocalActor );
  
  // Since the XMPP Network Server will push new addresses as they indicate 
  // their presence in the MUC, it is necessary to capture these messages in 
  // a dedicated handler.
  //
  // This will check if there is a resolver for this actor installed. This can 
  // happen if some local actor started to send messages to this remote actor 
  // before it came on-line. These messages will then be queued by the resolver 
  // for this remote actor until this message handler is invoked (possibly 
  // indefinitely if the actor never becomes available). If no resolver is
  // pending, then the address will just be recorded. 
  
  void RemotePeer( const Link::AvailabilityStatus & Peer, 
		   const Theron::Address TheNetworkLayer );

  // It may happen that a remote actor forgets to set the resource field, i.e.
  // it has formally no actor address. Then we will assume that the actor 
  // address equals the Jabber ID "name", and create a named actor using this
  // provided that this actor name does not already exist on this node as a 
  // local actor such that this actor naming will cause a naming conflict.
  
  virtual Address ResolveUndefined( const ExternalAddress & ExAddress );
  
  // --------------------------------------------------------------------------
  // Outbound: message handling
  // --------------------------------------------------------------------------
  
  // The standard handler for outbound messages is sufficient, and we only 
  // need to create an outbound resolver if this is required because the remote
  // actor ID is unknown by the session layer.
  
  virtual Address CreateOutboundResolver( 
    const Address & UnknownActorID, const Address & NetworkServerActor,
    const Address & SessionServerActor  );

  // --------------------------------------------------------------------------
  // Inbound: message handling
  // --------------------------------------------------------------------------
  // The inbound situation is in many ways simpler because a remote sender will
  // only send a message when it knows about the availability (XMPP Presence)
  // of a local actor. Hence when the message arrives, it should immediately 
  // be routed to the destination actor. The special addressing scheme enables 
  // us to know immediately the Actor ID of the remote sender (the resource 
  // field of the Jabber ID)
  //
  // When a message arrives, the decode message will be called with the external
  // message received, and it should return a legal message payload for a 
  // serialised message. If the received message is just a command with some 
  // arguments, an empty string should be returned to indicate that this message
  // has already been handled. The current version simply returns the payload 
  // of the outside message.
  //
  // However, it will store the sender's actor ID and external address under the
  // assumption that this will almost equal a lookup in complexity and ensure 
  // that the remote actor's ID is know for the further message handling.
  
  virtual std::string DecodeMessage( const OutsideMessage & Datagram );

  // If the sender's address is unknown on this endpoint, a resolver object 
  // should be created engaging with the remote endpoint to obtain the actor 
  // ID of the sender. However, under the encoding of addresses and the use of
  // the MUC to indicate the presence of new, remote actors, the address of 
  // the remote sender should already be known, and its actor ID decoded from 
  // its Jabber ID by the New Remote Actor function above. Hence it is a serious
  // logical error if there is a request to create a new inbound resolver, and
  // the function will therefore just throw a standard logic_error exception 
  
  virtual Address CreateInboundResolver( 
																		const ExternalAddress & UnknownActorAddress,
																		const Address & PresentationLayerActor,
																		const Address & SessionLayerActor )
  {
    std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << "XMPP Inbound resolver should not be needed!";
								 
		throw std::logic_error( ErrorMessage.str() );
  }
  
  // --------------------------------------------------------------------------
  // Constructor and destructor
  // --------------------------------------------------------------------------
  // The constructor will start the actor and register the handlers. Note that 
  // the virtual handlers are already defined at the Session Layer and need 
  // not be defined again.
  
public:
  
  XMPPProtocolEngine( NetworkEndPoint * HostPointer, 
								      const std::string & Password, 
								      const std::string & ServerName = "SessionLayer" );
  
  virtual ~XMPPProtocolEngine()
  {
    DeregisterHandler( this, &XMPPProtocolEngine::RemotePeer );
  }
  
}; // End - XMPP Protocol Engine

/*=============================================================================

 Serialising XMPP Message

=============================================================================*/

// Not implemented as there is nothing special to be done for the XMPP 
// protocol. This is the way it should be, as the serialising should be fairly 
// standard.

/*=============================================================================

 XMPP Manager (Network Endpoint)

=============================================================================*/

// The XMPP Manager is responsible for creating the classes needed 
// for the communication. Basically it is only the constructor that needs 
// implementation to initiate the XMPP classes.

class Manager : public Theron::NetworkEndPoint
{
private:
  
 // JabberID ActorDiscoveryMUC;
  
public:

  friend class Initialiser;
  
  class Initialiser : public Theron::NetworkEndPoint::Initialiser
  {
  protected:
    
    std::string ServerPassword;
    JabberID    InitialRemoteEndpoint;
    
  public:
    
    // The virtual functions are straightforward, but the syntax is lengthy 
    // so they are implemented in the source file.
    
    virtual void CreateServerActors ( void );
    virtual void BindServerActors   ( void );
    
    // The constructor needs the password for the server and an initial remote
    // peer to connect to. The latter can be omitted if this is the first 
    // network endpoint created for the actor system.
    
    Initialiser( const std::string & Password,
		 const JabberID & AnotherPeer = JabberID()  )
    : Theron::NetworkEndPoint::Initialiser(),
      ServerPassword( Password ), InitialRemoteEndpoint( AnotherPeer )
    {  }
    
    // The Jabber ID and the string will destruct by automatic invocation of
    // their destructor functions, and hence there is nothing to do explicitly
    // in the destructor. Yet it is important for correct inheritance.
    
    virtual ~Initialiser( void )
    { }
  };
  
  // The constructor is only invoking the end point constructor and forward  
  // the Initialiser object to the base class. Note that the initialiser must
  // be set by the Network End Point's Set Initialiser function when this 
  // constructor is called.
  
  Manager( const std::string & Name, const std::string & Location,
				   Theron::NetworkEndPoint::InitialiserType & TheInitialiser )
  : Theron::NetworkEndPoint(Name, Location, TheInitialiser )
  { }
  
  // The virtual destructor is currently not doing anything
  
  virtual ~Manager(void)
  { }
  
}; 		 // End XMPP::NetworkEndpoint

}      // End namespace XMPP
}      // End namespace Theron
#endif // THERON_XMPP_COMMUNICATION
