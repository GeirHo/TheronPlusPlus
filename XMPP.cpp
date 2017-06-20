/*=============================================================================
  XMPP - Extensible Messaging and Presence Protocol 

  This file contains the implementation of the methods for the Theron XMPP
  support classes as defined in the header file. 
  
  ACKNOWLEDGEMENT:
  
  The author is grateful to Dr. Salvatore Venticinque of the Second University
  of Naples for the selection of the Swiften library, and for guiding 
  implementations of similar mechanism for the CoSSMic project that has been 
  the basis for the Network Layer implementation here. Without his kind help 
  this XMPP interface would never have happened.
      
  Author: Geir Horn, University of Oslo, 2015, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include "XMPP.hpp"

// In order to facilitate the construction of a thread invoking a member 
// function on a class, use the reference utility class from the standard
// library found in the functional header.

#include <functional>

// The standard vector misses a find member function, and therefore the find
// function from the algorithms will be used instead.

#include <algorithm>

// Even though it is strictly unnecessary we include the boost shared pointers
// which is probably included by Swiften because it is based on Boost shared 
// pointers and not the standard library shared pointers. Unfortunately, 
// boost smart pointers cannot be converted to standard library smart pointers
// without deploying a clever trick, see
// http://stackoverflow.com/questions/12314967/cohabitation-of-boostshared-ptr-and-stdshared-ptr
// and therefore there are places in this implementation where boost smart 
// pointers must be directly used.

#include <boost/smart_ptr.hpp>

// Some networking headers are needed if we want to look up the IP of a symbolic
// address. This can be useful to detect if two clients are at the same server.
/*
#include <list>
#include <netdb.h>
#include <netinet/in.h>
*/
// Some error messages need formatted output enabled by the output string 
// stream.

#include <ostream>  // For error messages
#include <iostream> // TEST debugging messages

// Swiften returns a boost optional for the message body and it may be necessary
// to print this in error messages without first converting the optional to 
// a legal string. Hence, the IO support for optionals is included.

#include <boost/optional/optional_io.hpp>

namespace Theron {
namespace XMPP {
  
/*=============================================================================
  Link Server
  
  The constructor and destructor are implemented first and then follows the 
  various handlers for the interaction with the protocol engine
=============================================================================*/

// ---------------------------------------------------------------------------
// Utility functions
// ---------------------------------------------------------------------------

// The new peer function simply looks up the local ID, and calls the client 
// record's new peer function if there is a client record for this ID. 
// Otherwise it throws an invalid argument exception.

void Link::NewPeer( const JabberID & LocalID, const JabberID & RemoteID )
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

// In a similar way will the remove peer function throw if the local peer is 
// not available.

void Link::RemovePeer( const JabberID & LocalID, const JabberID & RemoteID )
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

// There is a need to figure out if the given agent discovery room is on the 
// local server or on a remote server. This could be as simple as just comparing 
// domains of the discovery server Jabber ID and the Jabber ID of the local 
// agent. However, it is not as easy as that since there is an old XMPP 
// convention to give extra modules sub-domains on the server. Hence a 
// Multi-User Chat (MUC) room on "server.location.eu" could conventionally be
// called "muc.server.location.eu". Hence, we have to compare the IP addresses
// of these servers. The official XMPP client connection (RFC 3920) port is 
// 5222 hence, hence the lookup is based on this port.
// 
// The actual lookup is done in two steps: First the server domain is converted
// to an IP address string, and then these two strings are compared. To look
// up the address of a server the getaddrinfo [3] function will be used and 
// the present code is basically the very nice example in Beej's Guide [4]. 
// TODO Implement cross platform support for Windows following TheComet's 
// example in his answer [5].
//
// REFERENCES:
// [3] http://linux.die.net/man/3/getaddrinfo
// [4] http://www.beej.us/guide/bgnet/output/html/singlepage/bgnet.html#getaddrinfo
// [5] http://www.gamedev.net/topic/671428-c-cross-platform-resolve-hostname-to-ip-library/
//
// The IP address lookup function is implemented first. For comparing the 
// server address, the IPv4 address is preferred if it is available. However, 
// if it is not available, the IPv6 address will be returned. For the sake of 
// comparison this would be sufficient because if one server has an IPv4 
// address and the others only an IPv6 address, well, then the two servers 
// are defined to be unequal. It returns a list of addresses known for the 
// given host, with a preference to IPv4 addresses.

/*
std::list< std::string > GetIP( const std::string & Hostname )
{
  addrinfo 		   Parameters, * Results;
  int			   LookupStatus = 0;
  std::list< std::string > IPv4_Addresses, IPv6_Addresses;
  
  // We will first set the parameters to use in the search. It is done 
  // explicitly since some of these parameters may be needed in future 
  // extensions. See [3] for details.
  
  Parameters.ai_addr      = nullptr;
  Parameters.ai_addrlen   = 0;
  Parameters.ai_canonname = nullptr;
  Parameters.ai_family    = AF_UNSPEC;     // IPv4 and IPv6 addresses requested
  Parameters.ai_flags     = AI_ADDRCONFIG; // Only configured addresses returned
  Parameters.ai_next      = nullptr;
  Parameters.ai_protocol  = 0;
  Parameters.ai_socktype  = SOCK_STREAM;   // Could also be zero?
  
  // Then the lookup is performed, and if it fails the an exception is thrown
  // as there is no way to deal with an error (except trying again)
  
  LookupStatus = getaddrinfo( Hostname.data(), "5222", &Parameters, &Results );
  
  if ( LookupStatus != 0 )
  {
    std::ostringstream ErrorMessage;
    
    ErrorMessage << "IP address lookup failed because " 
		 << gai_strerror( LookupStatus );
		 
    throw std::runtime_error( ErrorMessage.str() );
  }
  
  // The Results now point to a linked list of addresses found for this host
  // and it can contain more than one entry if the host has both an IPv4 and 
  // an IPv6 address. 
  
  for ( addrinfo * SingleIP  = Results; SingleIP != nullptr; 
		   SingleIP  = SingleIP->ai_next )
    if ( SingleIP->ai_family == AF_INET )
    {
      // IPv4 address detected. It will be converted to human readable form 
      // and then stored as the IPv4 address string.
      
      char IPstring[ INET_ADDRSTRLEN ];
      
      sockaddr_in * BinaryAddress 
		    = reinterpret_cast< sockaddr_in * >( SingleIP->ai_addr );
			    
      inet_ntop( SingleIP->ai_family, &(BinaryAddress->sin_addr), 
		 IPstring, INET_ADDRSTRLEN );
      
      IPv4_Addresses.emplace_back( IPstring );
    }
    else
    {
      // An IPv6 address is reported and the logic is the same except that
      // the length of the textual address string is different and it is stored
      // in different structures.
      
      char IPstring[ INET6_ADDRSTRLEN ];
      
      sockaddr_in6 * BinaryAddress 
		     = reinterpret_cast< sockaddr_in6 * >( SingleIP->ai_addr );
			   
      inet_ntop( SingleIP->ai_family, &(BinaryAddress->sin6_addr),
		 IPstring, INET6_ADDRSTRLEN );
      
      IPv6_Addresses.emplace_back( IPstring );
    }
  
  // The list of returned addresses must be deleted so the memory can be reused
  
  freeaddrinfo( Results );
  
  // Then we return the IPv4 list if it contains addresses, otherwise we return 
  // the IPv6 list.
  
  if ( !IPv4_Addresses.empty() )
    return IPv4_Addresses;
  else
    return IPv6_Addresses;
}
*/

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------
//
// The first event is that a client has been registered with the local XMPP 
// server and this will trigger the connection of the client.

void Link::ClientRegistered( JabberID ClientID, 
			     Swift::Payload::ref RegistrationResponse, 
			     Swift::ErrorPayload::ref Error )
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

void Link::ClientConnected( JabberID ClientID )
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

void Link::SendPresence( JabberID FromClient, 
			 Swift::Presence::Type PresenceType, JabberID ToClient)
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

void Link::PresenceNotification( JabberID ClientID, 
				 Swift::Presence::ref PresenceReceived )
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
      // Every endpoint in the network has a resource called "endpoint" 
      // responsible for the first time connection to peers. This should not 
      // be registered as an addressable actor at the session layer since 
      // this actor address is not unique, and shall never be used by any 
      // local actors.
      
      if ( PresenceReceived->getFrom().getResource() != "endpoint" )
	Send( AvailabilityStatus( PresenceReceived->getFrom(), 
	      AvailabilityStatus::StatusType::Available ), SessionServer );
      
      // Then the new actor should also be known to the client, and if  
      // messages have been buffered waiting for this actor to become active 
      // they can now be forwarded.
      
      NewPeer( ClientID, PresenceReceived->getFrom() );
      break;
    case Swift::Presence::Unavailable :
      // Similar to the availability status, also unavailability statuses 
      // from endpoint resources should be ignored.
      if ( PresenceReceived->getFrom().getResource() != "endpoint" )
	Send( AvailabilityStatus( PresenceReceived->getFrom(), 
	      AvailabilityStatus::StatusType::Unavailable ), SessionServer );
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

void Link::InitialRoster( JabberID ClientID, 
			  Swift::RosterPayload::ref TheRoster, 
			  Swift::ErrorPayload::ref  Error )
{
  if (!Error )
    for ( auto RemoteActor : TheRoster->getItems() )
     SendPresence( ClientID, Swift::Presence::Available, RemoteActor.getJID() );
}


// Dispatching the roster means reading off each of the connected IDs and 
// send them back to the actor waiting for these. 

void Link::DispatchRoster( const Address WaitingActor, 
			   Swift::RosterPayload::ref TheRoster, 
			   Swift::ErrorPayload::ref Error )
{
  if (!Error )
  {
    Roster CurrentPeers;
    
    for ( auto RemoteActor : TheRoster->getItems() )
      CurrentPeers.insert( RemoteActor.getJID() );
    
    Send( CurrentPeers, WaitingActor );
  }
}


// ---------------------------------------------------------------------------
// Actor discovery
// ---------------------------------------------------------------------------

// The dispatch known peers method is the callback registered by a network 
// endpoint when it requests its roster on behalf of a newly joined endpoint.
// When the roster arrives it will contain the jabber IDs of the agents known 
// to this endpoint, and these will directly be forwarded to the new endpoint 
// as subscribe messages for it to subscribe to all known agents. 

void Link::DispatchKnownPeers( JabberID ThisEndpoint, JabberID NewEndpoint, 
     Swift::RosterPayload::ref TheRoster, Swift::ErrorPayload::ref Error)
{
 if ( !Error )
   for ( auto RemoteActor : TheRoster->getItems() )
     SendMessage( OutsideMessage( ThisEndpoint, NewEndpoint, 
				  RemoteActor.getJID().toString(),
				  "SUBSCRIBE" ) );
}

// The new endpoint will register the subscribe known peers method as its 
// handler for incoming messages. It will first check that the received message
// is really a subscription message, and if it is, then a subscription will be
// made to the remote actor whose Jabber ID is in the message body. It will 
// throw if the message is not a subscribe message since this situation should 
// not occur. 

void Link::SubscribeKnownPeers( JabberID NewEndpoint,
                                Swift::Message::ref XMPPMessage )
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

void Link::EndpointPresence( JabberID ClientID, 
			     Swift::Presence::ref PresenceReceived )
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

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------
//
// The handler to create new clients will also register the client object in 
// the unordered map of clients using the provided password, and then ensure
// that the client is properly started. Note that the function is robust, 
// in the sense that if it is requested that a client is created that does 
// already exist, it will not create a new client.

void Link::CreateClient( const Link::NewClient & Request, const Address From )
{
  JabberID NewClientID( Request.GetJID() );
  
  if ( Clients.find( NewClientID ) == Clients.end() ) 
  {
    auto TheClient = std::make_shared< Swift::Client >( 
		     NewClientID, Request.GetPassword(), &NetworkManager ); 
 
    if ( ClientsOptions.useTLS == Swift::ClientOptions::NeverUseTLS )
      TheClient->setAlwaysTrustCertificates();
    
    // Setting event handler for the presence messages
    
    TheClient->onPresenceReceived.connect( 
      [=] (Swift::Presence::ref PresenceMessage)->void {
				  PresenceNotification( NewClientID, PresenceMessage );
      });

    // As this client serving a normal actor should just announce its 
    // availability when it is connected since the subscription to other 
    // endpoints should already be done by the endpoint client. The availability
    // message should be a broadcast to all other endpoints, and it is 
    // therefore sent with no destination.
    
    TheClient->onConnected.connect(
      [=] (void)->void { 
					SendPresence( NewClientID, Swift::Presence::Available ); 
      });
	
    // If the client has a resource field it is an actor with its own set of 
    // messages it can accept, and then the standard message processing 
    // handler is called. Basically this will just forward the message to 
    // the session layer so that the addresses can be correctly decoded and 
    // the message payload forwarded to the right local actor.
  
    TheClient->onMessageReceived.connect( 
      [=](Swift::Message::ref XMPPMessage)->void {
				  ProcessMessage( 
				    OutsideMessage( XMPPMessage->getFrom(), XMPPMessage->getTo(),
			                        XMPPMessage->getBody().value(), 
			                        XMPPMessage->getSubject()
				    ));
      });
         
    // All parts of the client record has now been initialised and it can be
    // stored in the internal map of connected clients
    
    Clients.emplace( NewClientID, 
		     ClientRecord( TheClient, Request.GetPriority() ) );

    // Then the client can be connected and the handlers should deal with the 
    // future events.

    TheClient->connect( ClientsOptions ); 
  }
}

// Destroying a client is simply to take it out of the client map and 
// delete it. Nothing happens if the client does not exist. It is assumed that
// the client destructor ensures that the client is disconnected from the 
// server and potentially that presence messages are sent to the remote 
// clients.

void Link::DestroyClient(const Link::DeleteClient & Request, const Address From)
{
  auto CurrentClient = Clients.find( Request.GetJID() );
  
  if ( CurrentClient != Clients.end() )
    Clients.erase( CurrentClient );
}

// When an actor needs to know its possible communication partners it will 
// request the roster of the corresponding client. This request will initiate 
// a call to return the roster, with the dispatch rooster handler registered 
// to be invoked when the roster has been returned from the XMPP server.

void Link::RequestRoster( const Link::GetRoster & Request, const Address From )
{  
  RosterRequest( Request.GetJID(), 
   [=](Swift::RosterPayload::ref TheRoster, Swift::ErrorPayload::ref Error)
      {	DispatchRoster( From, TheRoster, Error ); }
  );
}

// -----------------------------------------------------------------------------
// NORMAL MESSAGES ==> XMPP::Link
// -----------------------------------------------------------------------------

// Sending a message to a remote actor implies first creating the XMPP message 
// and setting the right fields, before dispatching the XMPP message on the 
// right local client. If the message is not from this client an logic error
// exception will be thrown.

void Link::ClientRecord::SendMessage( const OutsideMessage & Message )
{
  if ( TheClient->getJID() == Message.GetSender() )
    if ( ActivePeers.find( Message.GetRecipient() ) != ActivePeers.end() )
    {
      Swift::Message::ref XMPPMessage = Swift::Message::ref(
	new Swift::Message() );
      
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

void Link::SendMessage( const OutsideMessage & TheMessage )
{
  auto TheClientRecord = Clients.find( TheMessage.GetSender() );
  
  if ( TheClientRecord != Clients.end() )
    TheClientRecord->second.SendMessage( TheMessage );
}

// The new peer first registers the peer as an active peer for this client and 
// then calls the send function when emptying the queue of messages that have 
// been waiting for the remote actor to become available. 

void Link::ClientRecord::NewPeer( const JabberID & ThePeer )
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

// Conversely, if a peer indicates that it is no longer available, it should 
// be removed from the list of active peers. In this case there should of 
// course not be any messages waiting, so it is sufficient just to remove the 
// peer.

void Link::ClientRecord::RemovePeer( const JabberID & ThePeer )
{
  ActivePeers.erase( ThePeer );
}

// -----------------------------------------------------------------------------
// CONSTRUCTOR & DESTRUCTOR
// -----------------------------------------------------------------------------

// The constructor first starts the event manager being the simple event loop,
// then it starts the network manager passing it the address of the event 
// manager before the event manager is started in the thread.

Link::Link( NetworkEndPoint * Host, 
	    std::string ServerPassword,
	    const JabberID & InitialRemoteEndpoint,
	    std::string ServerName )
: Actor( Host->GetFramework(), ServerName.data() ),
  StandardFallbackHandler( Host->GetFramework(), ServerName ),
  NetworkLayer< Theron::XMPP::OutsideMessage >( Host, ServerName ),
  EventManager(), NetworkManager( &EventManager ),
  CommunicationLink( &Swift::SimpleEventLoop::run, std::ref(EventManager) )
{
  // Setting options for the clients. In this version we assume a secure 
  // and trusted network so we will not use TLS, and if TLS is switched off,
  // then the clients will also always trust the provided certificates.
  
  ClientsOptions.allowPLAINWithoutTLS = true;
  ClientsOptions.useTLS = ClientsOptions.Swift::ClientOptions::NeverUseTLS;
  
  // Register command handler functions for the XMPP specific commands 
  // received from the protocol engine. Note that the handler for normal 
  // outbound messages is provided by the Network Layer and we do not need to 
  // overload this here
  
  RegisterHandler( this, &Link::CreateClient );
  RegisterHandler( this, &Link::DestroyClient );
  RegisterHandler( this, &Link::RequestRoster );

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
  
  JabberID ThisEndpoint( Host->GetName(), Host->GetDomain(), "endpoint" );

  auto TheClient = std::make_shared< Swift::Client >( 
		   ThisEndpoint, ServerPassword, &NetworkManager ); 

  if ( ClientsOptions.useTLS == Swift::ClientOptions::NeverUseTLS )
    TheClient->setAlwaysTrustCertificates();

  // Setting special event handler for the presence messages that will take 
  // care of forwarding the roster of this endpoint to other endpoints that 
  // may come available in the future.

  TheClient->onPresenceReceived.connect( 
    [=] (Swift::Presence::ref PresenceMessage)->void {
				EndpointPresence( ThisEndpoint, PresenceMessage );
    });

  // Then this network endpoint will simply subscribe to this known endpoint
  // as soon as it is connected to the local XMPP server. However, it could
  // be that this endpoint is the very first endpoint in the system and 
  // in this case the known endpoint address will be invalid, and nothing 
  // should happen when we connect.
  
  if ( InitialRemoteEndpoint.isValid() )
    TheClient->onConnected.connect( [=](void)->void { 
					SendPresence( ThisEndpoint, Swift::Presence::Type::Subscribe,
		      InitialRemoteEndpoint );
    });

  // A network endpoint should only receive messages from other network 
  // endpoints asking the new network endpoint to subscribe to the other 
  // known endpoints. Hence we bind the hander to make the subscriptions 
  // as the message handler.
  
  TheClient->onMessageReceived.connect(
    [=](Swift::Message::ref XMPPMessage)->void {
				SubscribeKnownPeers( ThisEndpoint, XMPPMessage );
    });

  // All parts of the client record has now been initialised and it can be
  // stored in the internal map of connected clients. A subtle point is that 
  // this client is registered with +128 as priority. This priority will be 
  // used for all presence messages sent from this client. A higher priority 
  // means that the client is more likely to receive messages that are sent 
  // with a bare Jabber ID, typically the subscribe message from joining peers. 
  
  Clients.emplace( ThisEndpoint, ClientRecord( TheClient, 128 ) );

  // The first client must be registered before it can be connected and used.
  // Other actors will just be resources on this network endpoint, and will 
  // therefore not need a separate registration with the server.

  TheClient->connect( ClientsOptions );   
}

// The destructor will first ask the event manager to stop, and then wait for 
// the thread running the manger to terminate.

Link::~Link()
{
  EventManager.stop();
  CommunicationLink.join();
}

/*=============================================================================
//
// XMPP Protocol Engine
//
=============================================================================*/

// Since the external XMPP address of an actor is formed as a resource on the 
// protocol engine, it can be easily constructed from the End Point local 
// unique actor address. After defining the actors Jabber ID, a request is 
// sent to the link server to create a XMPP client for this actor, before 
// the new actor is added to the list of known actors.
//
// It should be noted that the protocol engine is basically about the mapping
// of addresses from actor addresses to external addresses. Hence it is no 
// reason to wait for the actor's client to connect before the address pair 
// is stored. 

void XMPPProtocolEngine::RegisterActor (
  const SessionLayerMessages::RegisterActorCommand & Command, 
  const Address LocalActor )
{
  JabberID ActorJID( ProtocolID.getNode(), ProtocolID.getDomain(), 
		     LocalActor.AsString() ); 
  
  Send( Link::NewClient( ActorJID, ServerPassword ), GetNetworkLayerAddress() );
  
  StoreActorAddresses( LocalActor, ActorJID );
}

// When an actor is removing its external presence, the process is reversed
// by first requesting the XMPP Link Server to remove the client for this 
// actor, and then invoking the default behaviour of the handler taking 
// the actor out of the communication address map.

void XMPPProtocolEngine::RemoveActor( 
  const SessionLayerMessages::RemoveActorCommand & Command, 
  const Address LocalActor )
{
  JabberID ActorJID( ProtocolID.getNode(), ProtocolID.getDomain(), 
								     LocalActor.AsString() );
  
  Send( Link::DeleteClient( ActorJID ), GetNetworkLayerAddress() );
  
  SessionLayer< OutsideMessage >::RemoveActor(Command, LocalActor);
}

// This message handler will be invoked when the Network Server gets a presence
// message from a new actor signing into the MUC. If there is a resolver 
// waiting for this actor, the address will be forwarded to the solver that 
// will take care of forwarding its message queue; otherwise, the address will 
// simply be stored.
//
// Alternatively, the message may indicate that the remote actor has gone off-
// line for which the actor address is removed using the directly the remove 
// actor function of the session layer 

void XMPPProtocolEngine::RemotePeer( const Link::AvailabilityStatus & Peer, 
																     const Address TheNetworkLayer )
{
  Theron::Address RemoteID;
  
  // If the peer's Jabber ID has no resource field (is "bare"), it is not 
  // possible to define the remote actor address.
  
  if ( Peer.GetJID().isBare() )
  {
    // Despite not having a proper address, it may be possible to construct 
    // this from the jabber name, and this is done as part of the 'resolve 
    // undefined' mechanism of the store actor address method (see below).
    
    RemoteID = Address::Null();
    StoreActorAddresses( RemoteID, Peer.GetJID() );
  }
  else
  {
    // Since the Jabber ID has a resource field, we use this as the name of the 
    // actor, and then check if this is an indication of availability of a 
    // new actor or if it is the opposite = the remote actor has become 
    // unavailable.
    
    RemoteID = Address( Peer.GetJID().getResource().data() );
  
    if ( Peer.GetStatus() == Link::AvailabilityStatus::StatusType::Available )
    {
      auto OutResolver = OutboundResolutionActors.find( RemoteID );

      if ( OutResolver != OutboundResolutionActors.end() )
				Send( Peer.GetJID(), OutResolver->second );
      else
				StoreActorAddresses( RemoteID, Peer.GetJID() );
    }
    else
      SessionLayer< OutsideMessage >::RemoveActor(
											SessionLayerMessages::RemoveActorCommand(), RemoteID );
  }
}

// When the method to store actor addresses gets a Null address for the actor,
// it will try to produce a proper address calling the 'resolve undefined' 
// mechanism. It will construct the actor ID on the basis of the name part of 
// the Jabber ID, and if this is unique on this end point it is accepted as 
// a useful name, otherwise it will throw an invalid argument exception.

Address XMPPProtocolEngine::ResolveUndefined( const JabberID & ExAddress )
{
  Address RemoteID( ExAddress.getNode().data() );
  
  if ( Host->IsLocalActor( RemoteID ) )
  {
    std::ostringstream ErrorMessage;
    
    ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
						     << "Unable to construct actor ID for " << ExAddress
								 << " since an actor with ID " << RemoteID.AsString()
								 << " already exists on " << Host->GetName();
		 
    throw std::invalid_argument( ErrorMessage.str() );
  }
   
  return RemoteID;
}



// When the remote actor ID is unknown, i.e. it has not yet indicated its 
// presence in the MUC, a resolver will be created to queue up the messages 
// for this remote actor. It will simply create a standard outbound resolver 
// and return its address. It is important that the resolver will kill itself 
// when its operation is due, so it is intentional to create an actor and 
// forget about it!

Address XMPPProtocolEngine::CreateOutboundResolver(
  const Address& UnknownActorID, 
  const Address& NetworkServerActor, 
  const Address& SessionServerActor)
{
  OutboundAddressResolver * Resolver
   = new OutboundAddressResolver( GetFramework(), UnknownActorID, 
				  NetworkServerActor, SessionServerActor );
  
  return Resolver->GetAddress();
}

// The message is not really decoded, but the actor ID of the remote actor 
// (the sender) will be stored as only the XMPP Protocol Engine will know that
// the sender's actor ID is encoded as the resource of the received message.
//
// Fundamentally, it should not be necessary to store the address for each 
// message since the address should already be known from the XMPP presence 
// message triggering a message to the above New Remote Actor handler. 
// The first two lines are therefore commented out as it will make the reception
// of a message slightly more efficient.

std::string XMPPProtocolEngine::DecodeMessage( const OutsideMessage & Datagram )
{
  //Theron::Address RemoteID( Datagram.GetSender().getResource().data() );
  
  //StoreActorAddresses( RemoteID, Datagram.GetSender() );
  
  return Datagram.GetPayload();
}

// The constructor is also fairly simple in that it defines the root user 
// address of the XMPP session layer based on the user name given and the 
// endpoint information.

XMPPProtocolEngine::XMPPProtocolEngine( NetworkEndPoint * HostPointer,
																			  const std::string & Password,
																			  const std::string & ServerName )
: Actor( HostPointer->GetFramework(), ServerName.data() ),
  StandardFallbackHandler( HostPointer->GetFramework(), ServerName ),
  SessionLayer< OutsideMessage >( HostPointer, ServerName ),
  ProtocolID( HostPointer->EndPoint::GetName(), HostPointer->GetDomain() ),
  ServerPassword( Password )
{
  RegisterHandler( this, &XMPPProtocolEngine::RemotePeer );
}


/*=============================================================================
//
// XMPP Manager (Network Endpoint)
//
=============================================================================*/

// The various XMPP servers are created by the the virtual function called from
// the base class constructor.

void Manager::Initialiser::CreateServerActors( void )
{
  GetNodePointer()->Create< XMPP::Link >( NetworkEndPoint::Layer::Network, 
					  InitialRemoteEndpoint );
  
  GetNodePointer()->Create< XMPP::XMPPProtocolEngine >(
				  NetworkEndPoint::Layer::Session,
				  ServerPassword );
    
  GetNodePointer()->Create< Theron::PresentationLayer >( 
				    NetworkEndPoint::Layer::Presentation );
}

// After creating the server actors we can bind them by ensuring that they 
// know the addresses of the other actors as appropriate.

void Manager::Initialiser::BindServerActors( void )
{
  // Binding Network Layer and the Session Layer by telling the Network Layer 
  // actor the Theron address of the Session Layer actor.

  GetNodePointer()->Pointer< XMPP::Link >( NetworkEndPoint::Layer::Network )
	  ->SetSessionLayerAddress( 
	    GetNodePointer()->GetAddress( NetworkEndPoint::Layer::Session )   );
  
  // The Session Layer needs the address of both the Network Layer and the 
  // Presentation Layer, so we better obtain the pointer only once and set the 
  // two needed addresses using this pointer.
    
  auto ProtocolEnginePointer = 
       GetNodePointer()->Pointer< XMPP::XMPPProtocolEngine >( 
	  NetworkEndPoint::Layer::Session );
       
  ProtocolEnginePointer->SetNetworkLayerAddress( 
			 GetNodePointer()->GetAddress( 
			 NetworkEndPoint::Layer::Network ));
  ProtocolEnginePointer->SetPresentationLayerAddress( 
			 GetNodePointer()->GetAddress( 
			 NetworkEndPoint::Layer::Presentation ));
  
  // Finally the Presentation Layer needs the address of the Session Layer.
  
  GetNodePointer()->Pointer< Theron::PresentationLayer >( 
    NetworkEndPoint::Layer::Presentation )->SetSessionLayerAddress( 
      GetNodePointer()->GetAddress( NetworkEndPoint::Layer::Session ));
}
  
} // End namespace XMPP  
} // End namespace Theron
