/*==============================================================================
Zero Message Queue

The Zero Message Queue (ZMQ) [1] is a peer-to-peer network communication layer 
providing sockets for connecting network endpoints and allowing an actor system
to be distributed over multiple nodes. Hence, it should allow actors to exchange
messages based on unique actor names. Under the principle of transparent 
communication, it should not be necessary to rewrite any actor code, and the 
actors need not knowing the endpoint executing the actor. The only requirements
on the actor code is that the messages that are exchanged with actors 
potentially on remote remote nodes must be derived from the Serial Message 
class to support serialisation. Consequently, the actor should be derived from 
the de-serialising class to participate in the remote exchange.

A message for an actor whose name cannot be resolved to an actor executing on 
this endpoint will first be delivered to the Presentation Layer for 
serialisation, and then passed on to the Session Layer that translates the 
actor address to an address that can be routed externally, and finally the 
Network Layer sends the message to a remote endpoint. This header file and 
the corresponding source file implements the Network Layer based on the ZMQ.

There are three types of communication to consider:

1. Distribution control: These are messages exchanged between endpoints to 
   accept new endpoints to the actor system, and to resolve the location of 
   actors by name across the endpoints of the distributed actor system.
   
2. Actor-to-Actor messages representing the normal message exchange of the 
   actor system
   
3. Publish-Subscribe communication: One of the strengths of ZMQ is its 
   built-in support for one-to-many messaging where actors can subscribe 
   to event messages from an actor, and when this update is sent each 
   actor will get the update. In a purist view, it is not necessary to support
   this pattern at the network layer since the publishing actor should accept
   the subscriptions, keep a list of subscribers and send a message to each 
   of them to "publish" the event using normal actor-to-actor messages. However,
	 since ZMQ supports this pattern, and many event driven systems use ZMQ 
	 providing support for this pattern will allow actors to subscribe to data 
	 published by a remote system.

All of these situations will be covered by ZMQ sockets as follows: Each endpoint
has one ROUTER socket to received incoming messages, in addition it has a SUB 
socket subscribing to broadcast events from other endpoints. This is used for
actor address resolution, and the endpoint hosting the actor requested will 
reply and the communication actor-to-actor can start. Each endpoint has one 
DEALER socket for each other endpoint in the actor system, and every message 
to this remote endpoint is sent through the same DEALER socket.

It is currently assumed that the messages are passed via TCP, and if the 
communicating parts of the actor system are hosted on the same physical node,
a loop back via the local host should be used. It may seems to be slow and 
wasteful to use the TCP stack for such inter process communication when pure 
inter process communication (IPC) is supported by ZMQ. However, different 
operating systems implements IPC differently, and several of them will do a 
network layer loop back behind the scene. The ZMQ direct IPC mechanism is 
supported only on systems supporting Unix Domain Sockets only implemented for 
POSIX compliant systems. More experimentation is needed for validating the 
performance hit of using TCP loop back for IPC, and if support should be 
provided.

The ZMQ library is written in C, and there are a few high level bindings for 
C++. The official binding cppzmq [2] is C-style code really not using the 
features of modern C++. The azmq [3] is aimed for implementations combining ZMQ 
communication with the Boost Asio [4] library. There were thus only two good 
candidates for the use with this implementation: CpperoMQ [5] and zmqpp [6]. 
CpperoMQ is an elegantly written header only library and targets sending of 
multi-part messages by overloading virtual functions to encode or decode the 
message parts. Unfortunately, CpperoMQ seems not to be updated for 
ZMQ version 4.x. The zmqpp library requires installation, but it has been 
updated with support for ZMQ version 4.x and does support multi-part messages 
through overloaded stream operators. It also provides a reactor to monitor 
activity on multiple sockets which can be dynamically added, in contrast with 
CpperoMQ that only supports polling on sockets defined at compile time. The 
zmqpp is therefore selected as the basis for this implementation, and it must 
be installed and the files can be included from

-I $(zmqppdir)/src/zmqpp

with the following libraries to be given for the linker (in this order)

-lzmqpp -lzmq -lboost_system

It should also be noted that the default installation of zmqpp installs the 
shared library It should be noted that the under /usr/local/lib and so that 
directory must be used by the linker. There are many ways to do this, please 
see Bob Plankers' excellent discussion [14].
   
References:

[1] http://zeromq.org/
[2] https://github.com/zeromq/cppzmq
[3] https://github.com/zeromq/azmq
[4] http://www.boost.org/doc/libs/1_64_0/doc/html/boost_asio.html
[5] https://github.com/jship/CpperoMQ
[6] https://github.com/zeromq/zmqpp
[7] https://en.wikipedia.org/wiki/List_of_TCP_and_UDP_port_numbers
[8] https://www.iana.org/
[9] https://mosh.org/
[10] https://en.wikipedia.org/wiki/IPv6
[11] https://linux.die.net/man/3/getifaddrs
[12] https://stackoverflow.com/questions/212528/get-the-ip-address-of-the-machine
[13] https://linux.die.net/man/3/inet_ntop
[14] https://lonesysadmin.net/2013/02/22/error-while-loading-shared-libraries-cannot-open-shared-object-file/
[15] https://rfc.zeromq.org/spec:29/PUBSUB/
[16] http://hintjens.com/blog:37

Author and Copyright: Geir Horn, 2017
License: LGPL 3.0
==============================================================================*/

#ifndef THERON_ZERO_MESSAGE_QUEUE
#define THERON_ZERO_MESSAGE_QUEUE

#include <string>							// Standard strings
#include <sstream>						// Stream operators on strings
#include <map>								// Storing sockets for remote endpoints.
#include <unordered_map>			// Last good value cache for data publishers
#include <list>               // For temporary storing subscription requests
#include <memory>             // Shared pointers
#include <typeinfo>						// For published message types
#include <typeindex>          // To store typeIDs in containers
#include <type_traits>        // For useful meta programming

#include <zmqpp.hpp>					// ZeroMQ bindings for C++
#include <boost/asio/ip/address.hpp> // IP address class

#include "Actor.hpp"					// The Theron++ actor framework
#include "NetworkEndPoint.hpp"// The host for the endpoint servers
#include "LinkMessage.hpp"		// The interface for the link message
#include "NetworkLayer.hpp"   // The generic network layer interface
#include "StandardFallbackHandler.hpp" // Catching wrongly sent messages if any
#include "SerialMessage.hpp"  // Base class for serial network messages
#include "DeserializingActor.hpp"      // Actor receiving serial messages

namespace Theron::ZeroMQ
{
/*==============================================================================

 TCP ports and addresses

==============================================================================*/
//
// Assigning the TCP ports to be used by the actor system on the endpoints is 
// a matter of choice. Not all ports can be freely used, and many ports are 
// already registered for various server applications [7]. Given that it is 
// not possible to know what other applications that are running on the 
// computers hosting the actor system, one should only use ports from the 
// dynamic or private range (49152–65535) that cannot be registered 
// with IANA [8]. The mobile SSH like shell Mosh [9] uses the port range 
// 60000–61000. Hence, this range should be avoided. Furthermore, the port 
// range should be easy to remember, and normally ranges are continuous. Given
// these restrictions and considerations the following ports are chosen.

constexpr auto PublisherPort = 50505;
constexpr auto InboundPort 	 = 50506;
constexpr auto OutboundPort  = 50507;	// Is this really needed?
constexpr auto DataPort      = 50508; // Publish data messages

// In order to ensure that the address confirms to the right format, a special 
// class will be used. It uses the IP address class is taken from the Boost 
// ASIO library in order to be fully portable and cover both IPv4 and IPv6.

using IPAddress = boost::asio::ip::address;

// The local host has a particular meaning. It is used to indicate that an 
// endpoint is the first endpoint of the system that should not connect to
// other peers, and it may be used to test the system in loop-back mode. It 
// is not possible to define a class as a constant expression, but it can be 
// returned from a constant expression function.

inline IPAddress LocalHost( void )
{ return IPAddress::from_string( "127.0.0.1" ); }

// With the IP address type defined, it is easy to define a TCP endpoint that 
// consists of an IP address and a port number.

class TCPAddress
{
private:
	
  IPAddress          IP;
	unsigned short int PortNumber;
	
public:
	
	// The fundamental constructor takes these quantities in their binary format
	// and possibly acting as a default constructor even though the TCP endpoint 
	// does not make any sense if it is default constructed.
	
	inline TCPAddress( const IPAddress & TheIP = IPAddress(), 
										 unsigned short int ThePort = 0 )
	: IP( TheIP ), PortNumber( ThePort )
	{ }
	
	// However, one or both of these may be given as strings and there are 
	// converters to ensure the correct construction in these cases.
	
	TCPAddress( const std::string & TheIP, unsigned short int ThePort );
	
	inline TCPAddress( const IPAddress & TheIP, const std::string & ThePort )
	: TCPAddress( TheIP, std::stoi( ThePort ) )
	{ }
	
	inline TCPAddress( const std::string & TheIP, const std::string & ThePort )
	: TCPAddress( TheIP, std::stoi( ThePort ) )
	{ }
	
	// The most difficult conversion occurs if the whole address is given as a 
	// single string as it must be parsed and verified that the various parts 
	// are present, and in the right format. The implementation is therefore in 
	// the code file.
	
	TCPAddress( const std::string & TheAddress );
	
	// Then there are copy, move and assignment constructors
	
	inline TCPAddress( const TCPAddress & Other )
	: TCPAddress( Other.IP, Other.PortNumber )
	{ }

	inline TCPAddress( const TCPAddress && Other )
	: TCPAddress( Other.IP, Other.PortNumber )
	{ }
	
	inline TCPAddress & operator = ( const TCPAddress & Other )
	{
		IP 				 = Other.IP;
		PortNumber = Other.PortNumber;
		
		return *this;
	}
	
	// Then it is possible to ensure that the string representation of the TCP 
	// address is correct.
	
	inline std::string AsString( void ) const
	{
		return "tcp://" + IP.to_string() + ":" + std::to_string( PortNumber );
	}
	
	// Then there are interface functions to get the IP and port number
	
	inline IPAddress GetIP( void ) const
	{ return IP; }
	
	inline unsigned short int GetPort( void ) const
	{ return PortNumber; }
	
	// Comparators to allow the sorting of addresses. They are first compared by 
	// the IP address and if the IP address equals, then by the port number. Both
	// must be equal for the TCP addresses to be equal
	
	inline bool operator == ( const TCPAddress & Other ) const
	{
		return (IP == Other.IP) && (PortNumber == Other.PortNumber);
	}
	
	inline bool operator != ( const TCPAddress & Other ) const
	{
		return (IP != Other.IP) || (PortNumber != Other.PortNumber);
	}
	
	inline bool operator < ( const TCPAddress & Other ) const
	{
		if ( IP < Other.IP )
			return true;
		else
			if ( (IP == Other.IP) && (PortNumber < Other.PortNumber) )
				return true;
			else
				return false;
	}
};

/*==============================================================================

 Network address

==============================================================================*/
//
// The address must essentially contain the network ID of the endpoint and
// and the symbolic name of the actor on that endpoint. 

class NetworkAddress
{
private:
	
	  TCPAddress  EndpointID;
		std::string ActorID;
	
public:
	
	// The constructor is used to initialise the IDs. Note that the actor ID can 
	// be given as an address, and a separate constructor is provided for this 
	// situation.
	
	inline NetworkAddress( const TCPAddress & TheEndpoint, 
												 const Address TheActor )
	: EndpointID( TheEndpoint ), ActorID( TheActor.AsString() )
	{ }
	
	inline NetworkAddress( const TCPAddress & TheEndpoint, 
												 const std::string & TheActor )
	: EndpointID( TheEndpoint ), ActorID( TheActor )
	{ }
	
	inline NetworkAddress( const std::string & TheEndpoint, 
												 const std::string & TheActor )
	: NetworkAddress( TCPAddress( TheEndpoint ), TheActor )
	{ }
	
	NetworkAddress( const std::string & TheEndpoint, const Address & TheActor )
	: NetworkAddress( TheEndpoint, TheActor.AsString() )
	{ }
	
	// The copy and move constructors are also needed, as well as the assignment 
	// operator
	
	NetworkAddress( const NetworkAddress & Other )
	: NetworkAddress( Other.EndpointID, Other.ActorID )
	{ }
	
	NetworkAddress( const NetworkAddress && Other )
	: NetworkAddress( Other.EndpointID, Other.ActorID )
	{ }
	
	inline NetworkAddress & operator = ( const NetworkAddress & Other )
	{
		EndpointID = Other.EndpointID;
		ActorID    = Other.ActorID;
		
		return *this;
	}
	
	// The address might need to be default constructed if it is to be read from 
	// an incoming message. It has to explicitly set the endpoint ID to an illegal
	// address since the TCP Address object deliberately does not have a default 
	// constructor.
	
	NetworkAddress( void )
	: EndpointID(), ActorID()
	{ }

  // ---------------------------------------------------------------------------
  // Interface functions
  // ---------------------------------------------------------------------------
  
  inline IPAddress GetIP( void ) const
	{ return EndpointID.GetIP(); }
  
  inline std::string GetEndpoint( void ) const
	{ return EndpointID.AsString(); }
	
	inline TCPAddress GetEndpointLocation( void ) const
	{ return EndpointID; }
	
	inline std::string GetActorName( void ) const
	{ return ActorID; }
	
	inline Address GetActorAddress( void ) const
	{ return Address( ActorID ); }

  // ---------------------------------------------------------------------------
  // Comparators
  // ---------------------------------------------------------------------------
	//
	// In order to ensure proper routing of messages, it it necessary to be able 
	// to compare addresses. Both the endpoint ID and the actor ID must match for
	// two addresses to be considered equal.
	
	inline bool operator == ( const NetworkAddress & Other )
	{
		return (EndpointID == Other.EndpointID) && (ActorID == Other.ActorID);
	}
	
};

/*==============================================================================

 Outside message

==============================================================================*/
//
// The outside message must minimally contain the sender address, the receiver 
// address and the serialised payload. The interface is defined by the link 
// message class, and it based on the above network address for supporting the 
// external address. 

class OutsideMessage : public LinkMessage< NetworkAddress >
{
public:
	
  // ---------------------------------------------------------------------------
  // Message types
  // ---------------------------------------------------------------------------
  //
	// The outside message will be interpreted by the remote network layer actor
	// and processed accordingly. It is transmitted as the first frame of the 
	// ZMQ message, and if the first frame does not decode to one of the types,
	// it is taken to be the payload and the message type is set to Data.
	
	enum class Type
	{
		Subscribe,				// Ask all other endpoints to subscribe to a new peer
		Address,					// Start address resolution or provide an address
		Remove,           // An actor is being removed from the system
		Roster,						// IP addresses of the peers to subscribe to
		Message,					// Normal message between two actors
		Data							// Raw data
	};
	
  // ---------------------------------------------------------------------------
  // Message content
  // ---------------------------------------------------------------------------
	
private:
	
	Type MessageType;
	
  // ---------------------------------------------------------------------------
  // Interface functions
  // ---------------------------------------------------------------------------

public:
	
	inline Type GetType( void ) const
	{ return MessageType; }
	
	// There must also be a function to convert the external address to an actor 
	// address. This is just to return the actor address as returned by the 
	// network address itself.
	
	virtual 
	Address ActorAddress(const NetworkAddress & ExternalActor) const override
	{
		return ExternalActor.GetActorAddress();
	}

  // ---------------------------------------------------------------------------
  // Constructors & destructor
  // ---------------------------------------------------------------------------
	
	inline OutsideMessage( Type Category, const NetworkAddress & From, 
												 const NetworkAddress & To, 
												 const SerialMessage::Payload & ThePayload 
														 = SerialMessage::Payload() )
	: LinkMessage< NetworkAddress >( From, To, ThePayload ), 
	  MessageType( Category )
	{ }
	
	// Copy, move and assign from other outside messages
	
	inline OutsideMessage( const OutsideMessage & Other )
	: OutsideMessage( Other.MessageType, Other.SenderAddress, 
										Other.ReceiverAddress, Other.Payload )
	{ }
	
	inline OutsideMessage( const OutsideMessage && Other )
	: OutsideMessage( Other.MessageType, Other.SenderAddress, 
										Other.ReceiverAddress, Other.Payload )
	{ }
	
	// There is a reduced compatibility constructor to have the same parameters
	// as for the standard link message operator. In this case the message type 
	// will be set to "Message"
	
	OutsideMessage( const NetworkAddress & From, const NetworkAddress & To, 
									const SerialMessage::Payload & ThePayload 
									    = SerialMessage::Payload() )
	: OutsideMessage( Type::Message, From, To, ThePayload )
	{ }
	
	// The default constructor is used when delayed initialisation through the 
	// function operator is used. The Session Layer server does this when sending
	// messages to the link layer.
	
	OutsideMessage( void ) = delete;

	// Since the message contains virtual functions it needs a virtual destructor
	
	virtual ~OutsideMessage( void )
	{ }

  // ---------------------------------------------------------------------------
  // Transmitting the message over the network
  // ---------------------------------------------------------------------------
	//
	// It is more interesting that the message can be completely constructed from 
	// a ZMQ message, which is used when receiving the message. 
	
	OutsideMessage( zmqpp::message & ReceivedMessage );
	
	// There is a send function taking a sending socket as argument sending the 
	// message as a multi-part message.
	
	void Send( zmqpp::socket & NetworkSocket ) const;
	
	// When sending the message over the network it is necessary to encode the 
	// message type. This is done as a string.
	
	std::string Type2String( void ) const;
	
}; // End outside message

/*==============================================================================

 Link server

==============================================================================*/
//
// The link server implements the network layer using the ZMQ protocol by 
// exchanging outside messages with other remote link servers, and with the 
// session layer server on the same endpoint. 
//

class Link : virtual public Actor,
						 virtual public StandardFallbackHandler,
						 public NetworkLayer< OutsideMessage >
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
	
	static zmqpp::context ZMQContext;				// ZMQ server thread
	zmqpp::socket         Publish,				  // Broadcast socket
								        Subscribe,        // Subscription to events
							          Inbound;					// Router for inbound messages
  static zmqpp::reactor SocketMonitor;    // To react to incoming messages
							   
  // The link server will cache its own external network address for sending 
	// control messages.
	
	static NetworkAddress OwnAddress;
	
	// Strong encapsulation is desired, and for derived classes to be able to 
	// create sockets in this context and monitor the activity on these sockets,
	// special read-only interface functions are provided.
	
protected:
	
	inline static zmqpp::context & GetContext( void )
	{ return ZMQContext; }
	
	inline static zmqpp::reactor & GetMonitor( void )
	{ return SocketMonitor; }
	
	inline static NetworkAddress GetNetworkAddress( void )
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
	
  std::map< IPAddress, zmqpp::socket > Outbound;
	
	// There is a utility functions to register a new peer and connect to it. 
	
	void AddNewPeer( const IPAddress & PeerIP );
	
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

  // When a local actor is closing it will be removed from the session layer, 
  // which will then inform the network layer about the removal so that other 
  // remote session layers can be informed that the actor has been removed.
															 
  virtual void ActorRemoval( const RemoveActor & TheCommand, 
														 const Address TheSessionLayer ) override;
	
	// The above resolution protocol will allow _local_ actors to obtain addresses
  // for themselves, and for their communication partners. However, it will 
  // not allow a remote sender to identify actors on this endpoint. For this a 
  // second protocol is needed. Basically it is the "mirror" of the above 
  // protocol:
	// 
  // When an address request arrives on the subscriber channel, the actor 
  // address will be picked out and forwarded as a resolution request to the 
  // session layer server. If and only if this actor exists on this endpoint,
  // a response will be returned. 
  //
  // The handler for this response will then complete the process by returning 
  // a message of "Address" type back to the requesting peer.
  // 
  // The existing resolution request and response messages cannot be reused 
  // because they will not remember the address of the requesting endpoint. 
  // This address could have been stored in a map so ensure that more requests
  // could be handled before the response eventually comes back, but this would 
  // require a response even if the requested actor was not on this endpoint to
  // avoid the map to contain actors that are on remote endpoints, i.e. that 
  // could not be resolved on this endpoint. Defining a special message format
  // carrying the requesting endpoint as a parameter will resolve this issue 
  // and allow for a non-response if the actor is not located on this endpoint.

public:
	
	class LocalActorInquiry
	{
	public:
		
		const NetworkAddress ActorRequested, 
												 RequestingEndpoint;
		
		inline LocalActorInquiry( const NetworkAddress & TheActor, 
														  const NetworkAddress & TheEndpoint )
		: ActorRequested( TheActor ), RequestingEndpoint( TheEndpoint )
		{ }
		
		inline LocalActorInquiry( const LocalActorInquiry & Other )
		: LocalActorInquiry( Other.ActorRequested, Other.RequestingEndpoint )
		{ }
		
		LocalActorInquiry( void ) = delete;
	};
		
private:
		
  void FoundLocalActor( const LocalActorInquiry & TheResponse, 
												const Address TheSessionLayer );
	
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

	// There is a handler for messages received on the inbound socket. Normal 
	// messages are forwarded to the session layer, and protocol messages when 
	// a new peer joins (see protocol described below) are dealt with directly.
	
private:
	
	void InboundMessageHandler  ( void );
	
	// The message handler for broadcast messages will handle subscription 
	// requests by connecting to the new peer, and forward address resolution 
	// commands to the the session layer.
																
	void SubscribedMessageHander( void );
	
  // ---------------------------------------------------------------------------
  // Constructor
  // ---------------------------------------------------------------------------
	//
	// The basic constructor takes the IP address of the first peer in the actor 
	// system and the actor system name for the link server on that node. For the 
	// first endpoint starting, the endpoint IP should be given as the IP address 
	// of the local host (127.0.0.1). For all other IP addresses, the link will 
	// send a subscribe message to the remote endpoint, see the new peer protocol 
	// above. By default is is assumed that all link servers have the same actor 
	// name since no actor shall send a direct message to a link server on a 
	// remote node. The constructor will then initiate the new node initialisation
	// process.
	//
	// When a node of the distributed actor system connects not as the first node,
	// it must be given the IP address of a remote peer that will help connecting 
	// the new node to the system. The new link server will then undertake the 
	// following actions: 
	//
	// A. Start the sockets to listen to the given ports on the local interfaces
	// B. Subscribe to the remote peer's publish socket
	// C. Set up an outbound socket for the remote peer, and connect this to the 
  //    known peer's inbound socket.
	// D. Send a 'subscribe' message to the remote link server on its inbound
	//    request socket (connected to in step C) containing the IP address of 
	//    the new node as payload
	// 
	// When he remote link server receives the 'subscribe' command it will
	//
	// 1. Publish the same 'Subscribe' message to all other peers connected to its
	//    publisher socket so that all other peers can start connecting to the 
	//    new peer.
	// 2. Create an outbound socket for the new remote peer
	// 3. Connect the outbound socket to the remote peer's inbound socket.
	// 4. Send a message to the new peer containing the IP addresses of all the 
	//    other peers in the system currently known and connected.
	// 
	// When a link server receives a 'subscribe' message on its subscriber socket 
	// it will connect to the new peer's publisher socket and create an outbound 
	// socket for this peer, and connect this to the new peer's inbound socket.
	//
	// When the new peer receives the response from the known peer containing the 
	// IP addresses of the other peers in the system, it will create outbound 
	// sockets for each peer, and connect them to the  peer's inbound socket and 
	// subscribe to the peer's publisher. 
	
public:
	
	Link( const IPAddress & InitialRemoteEndpoint, 
			  const std::string & RemoteLinkServerName = "ZMQLink", 
			  const std::string & ServerName = "ZMQLink" );
	
}; // End Link layer server class
   // Note that the link extension for publishing and subscribing is defined 
   // after the session layer definition.

/*==============================================================================

 Session layer

==============================================================================*/
//
// The main task of the session layer is to keep track of the external addresses
// of actors to allow messages to be sent to actors without knowing where the 
// actor is. Local actors with an external presence must register with the 
// session layer when starting, and de-register when closing. Local actors 
// will be assigned the TCP endpoint of this node.
//
// The standard session layer provides most of the functionality needed. 
// However, it serves the local actor with external addresses, and with 
// addresses of remote actors they want to exchange messages with. There is 
// no support for responding to the question that may be raised by remote 
// session layers: Is a given actor hosted on this node? 
//
// This functionality cannot be supported generically since it is not needed by
// all network layer protocols. Some protocols simply broadcast new actors to 
// all endpoints and then there is no need to resolve where a particular actor 
// is hosted. 
//
// The ZMQ link will send a local actor inquiry message to the session layer, 
// as described above. The session layer handler for this message will fill 
// the address if the actor exists on this endpoint and return the response 
// to the link. If the actor does not exist, the inquiry will just be forgotten.

class SessionLayer : virtual public Actor,
										 virtual public StandardFallbackHandler,
										 public Theron::SessionLayer< OutsideMessage >
{
private:
	
	void CheckActor( const Link::LocalActorInquiry & TheRequest, 
									 const Address TheLinkServer );
	
public:
	
	SessionLayer( const std::string & ServerName = "SessionLayer" )
	: Actor( ServerName ), StandardFallbackHandler( GetAddress().AsString() ),
	  Theron::SessionLayer< OutsideMessage >( GetAddress().AsString() )
	{
		RegisterHandler( this, &SessionLayer::CheckActor );
	}
};

/*==============================================================================

 Publishing

==============================================================================*/
//
// There are two sources for published data:
//
// 1. The data can be generated by an actor on the remote actor system 
//    endpoint. In this case the data will be encoded by the session layer 
//    as a "Data" type outside message. 
// 2. The data is published by some other system and is received just as a 
//    string. 
//
// In both cases the issue is at the receiving side since a subscriber socket
// does not tell where the data is coming from. Which is natural since it is 
// implementing a pure publish-subscribe pattern where the data source is 
// known from the subscription. It is allowed that the publisher has many 
// subscribers.
// 
// A subscription has a "topic" and a subscriber can subscribe to different 
// topics from different providers. However, the subscriber socket will not 
// tell us to which topic an incoming message belongs. 
//
// Finally, there is the Last Value Caching (LVC) issue: If a subscriber 
// to an infrequently updated value subscribes shortly after the last value 
// was published, then it can take a long time before the subscriber receives
// the next value, and this could potentially cause problems.
// 
// The ideal situation would be that each publishing actor has its own PUB 
// socket. This would necessitate a separate IP port for each actor, and this 
// cannot be done in a generic way. Hence the only option is that each endpoint 
// has one data publisher socket implemented as an XPUB socket. This is 
// configured to use the ZMQ_XPUB_VERBOSE so that the handler function is called
// every time an actor subscribes to a given topic. The handler function will 
// then retransmit the last good value cached for this topic. Unfortunately, 
// both the XPUB and the ZMQ_XPUB_VERBOSE are poorly documented. The ZMQ 
// Requests For Comments (RFC) give some basic behaviour [15], and the best 
// example is Pieter Hintjens example of a publisher that awaits a known number
// of subscriptions to connect before it starts to publish [16]
//
// The downside of this is that all subscribers will get the cached value when 
// new subscribers arrive. This could be undesirable if the arrival of a new 
// topic value triggers other activities by the receiving actor. The subscriber 
// should therefore check for identity updates and only forward the update to 
// the actor if the value is different from the current value.
//
// -----------------------------------------------------------------------------
// Publishing link
// -----------------------------------------------------------------------------
//
// The main purpose of the class is to manage a socket for publishing the data. 
// One could imagine that the publishing link would also maintain a Last Good 
// Value cache. A deep and philosophical observation should be noted: under a 
// publish-subscribe pattern the publisher will not know when other actors in 
// the system subscribe - or unsubscribe. Thus, it is possible to subscribe to 
// a topic with no publisher and receive silence, and it is possible to publish
// on a topic with no subscribers. For this reason the publishing link is never
// able to clear the cache of the last good value from a publisher because 
// even if the publisher does no longer exist (or we do not know when it will
// publish next), there may be subscribers wanting the last good value. 
// Furthermore, even if we know that there are no more subscribers to a topic,
// it does not preclude future subscribers from arriving. 
//
// Given that a managed topic corresponds to a message type, the number of 
// message types supported by an actor does not grow wildly. If there are 
// many different actor types in use the total number of topics will, at most,
// be the product of the number of actor instances of an actor type,
// times the the number of messages it supports, and this factor should be 
// added for all actor types. Hence the worst case can be difficult, and 
// if the payload size of the messages is large, it could be a significant 
// amount of data to keep in memory. 
//
// One approach could be to use a "garbage collection" type technique building
// on reference counting: When a topic is no longer subscribed to, it is a 
// candidate for cache deletion. Then two approaches would be possible:
// 
// 1. Delete the last good value for the topic when the total number of 
//    topics, or the cache size in bytes, reaches a threshold or watermark. 
//    In this case the topics with the largest payloads could be deleted 
//    first.
// 2. Record when the topic is published. If a topic has no subscribers and 
//    there time since last publication is longer than a certain time out 
//    limit, the topic should be deleted.
//
// The first is a size based strategy whereas the second is a time out 
// strategy. Both of these can alleviate the problem, but none is perfect.
//
// A truly different approach is needed based on the observation that if a 
// publishing agent is gone, there is no need to cache its topics! This holds
// irrespective of the number of subscribers for this topic. This implies 
// that the actor should cache its own last good value, and re-publish this 
// when its topic has a new subscriber. A further benefit is that the binary
// message can be cached since it will typically take less space than its 
// serialised counterpart. Consider for example a boolean that essentially 
// requires one binary bit, or five characters "true" or "false" or 8 bits if
// represented as a character 0 or 1.
//
// This approach is implemented in a base class "Publisher" offering one 
// method publish a message accessible for derived classes, and which runs 
// a protocol with the publishing link in the background:
//
// A. Published messages are serialised and sent as a message to the link 
//    actor. 
// B. The link actor sends a new subscriber message back to the actor when 
//    the actor has a new subscriber to one of its topics. When the actor 
//    gets this message, it will look up the last value for the topic and 
//    re-publish the last value.
// C. When the actor closes, its destructor will send a delete topic
//    message that will remove all references to its topics.
 
class PublishingLink : virtual public Actor,
											 virtual public StandardFallbackHandler,
											 public Link
{
	
private:
	
	zmqpp::socket DataPublisher;
	std::unordered_map< std::string, Address > Publishers;
	
	// The topic string is defined similar to a Jabber ID of the form 
	// "actorname@endpoint/messageID" since an actor may publish several 
	// different message types.
	
	inline static std::string SetTopic( const std::string & ActorName, 
																		  const std::string & EndpointID, 
																		  const std::string & MessageID )
	{
		return ActorName + "@" + EndpointID + "/" + MessageID;
	}
	
	// Then there are various interfaces to this string version. First one where
	// the actor the actor and topic is given and the local endpoint is assumed 
	// by default 
	
	template< class MessageType >
	static std::string SetTopic( const Address & TheActor )
	{
		std::string EndpointID( GetNetworkAddress().GetIP().to_string() ),
		            MessageID( typeid( MessageType ).name() );
								
		return SetTopic( TheActor.AsString(), EndpointID, MessageID );
	}
	
	// Another version takes both the actor and the endpoint as objects and then 
	// does the full conversion
	
	template< class MessageType >
	static std::string SetTopic( const Address & TheActor, 
															 NetworkAddress TheEndpoint )
	{
		return SetTopic( TheActor.AsString(), TheEndpoint.GetIP().to_string(), 
										 typeid( MessageType ).name() );
	}
	
  // ---------------------------------------------------------------------------
  // Supporting subscribers
  // ---------------------------------------------------------------------------
	//
  // A subscribing actor will need to create the subscriber socket in the same 
  // context as the other sockets, and it therefore needs access to the 
  // context and the socket monitor.
  
  using Link::GetContext;
	using Link::GetMonitor;
	
	// In order to access these the subscriber class is declared as a friend
	
  friend class Subscriber;
	
	// When a topic subscription arrives, it will be checked against the registry 
	// of publishers, and if the topic is known a message is sent to the owning 
	// actor to re-publish the cached last good value. The message simply contains
	// the topic string.
	
	class SendLastGoodValue
	{
	public:
		
		const std::string Topic;
		
		SendLastGoodValue( const std::string & GivenTopic )
		: Topic( GivenTopic )
		{ }
		
		SendLastGoodValue( const SendLastGoodValue & Other )
		: Topic( Other.Topic )
		{ }
		
		SendLastGoodValue( const SendLastGoodValue && Other )
		: Topic( Other.Topic )
		{ }
		
		SendLastGoodValue( void ) = delete;
	};
	
	// There is a handler called when there is a subscription event on the 
	// publisher socket, and this will send the message to the actor if there 
	// is already a publisher for this topic. Otherwise, it will do nothing.
	
	void SubscriptionEvents( void );
	
  // ---------------------------------------------------------------------------
  // Publishing a message
  // ---------------------------------------------------------------------------
	//
	// The publisher actor base class has to be a friend of this link in order to
	// engage in the last good value cache protocol outlined.
	
	friend class Publisher;
	
	// When an actor wants to publish a message it will implicitly send a message 
	// to this link actor containing the topic for which the message is published
	// and the serialised message content. The message structure is simple
	
	class PublishableMessage
	{
	public:
		
		const std::string Topic;
		const SerialMessage::Payload Payload;
		
		template< class MessageType >
		PublishableMessage( const MessageType & TheMessage, 
												const std::string & ActorName )
		: Topic( SetTopic< MessageType >( ActorName ) ),
		  Payload( TheMessage->Serialize() )
		{ }
		
		// When this is used to publish a message from the last good value cache, 
		// the topic is already given, and there is a pointer to the Serial Message
		// to be sent. In this case the constructor takes a different form.
		
		PublishableMessage( const std::string & TheTopic, 
												std::shared_ptr< SerialMessage > & TheMessage )
		: Topic( TheTopic ), Payload( TheMessage->Serialize() )
		{ }
		
		// Then there are trivial copy and move constructors.
		
		PublishableMessage( const PublishableMessage & Other )
		: Topic( Other.Topic ), Payload( Other.Payload )
		{ }
		
		PublishableMessage( const PublishableMessage && Other )
		: Topic( Other.Topic ), Payload( Other.Payload )
		{ }
		
		PublishableMessage( void ) = delete;
	};

	// This is served by a message handler that stores the new payload for this 
	// topic before forwarding the message to the publisher socket.
		
	void DispatchMessage( const PublishableMessage & TheMessage, 
												const Address ThePublisher );
	
  // ---------------------------------------------------------------------------
  // Closing a publisher
  // ---------------------------------------------------------------------------
	//
	// When a publisher actor closes, its destructor will send a message to 
	// indicate this to the link. It is the message type that carries the command.
	
	class ClosePublisher
	{
	public:
		
		ClosePublisher( void ) = default;
		ClosePublisher( const ClosePublisher & Other ) = default;
	};
	
	// The message handler for this will simply look up what topics has the 
	// sender actor as publisher, and erase these.
	
	void DeletePublisher( const ClosePublisher & TheCommand, 
												const Address ThePublisher );
	
  // ---------------------------------------------------------------------------
  // Constructor and destructor
  // ---------------------------------------------------------------------------
	//
  // The constructor takes the same parameters as the basic link server, and 
  // sets up the data publisher and binds it to the right port.

public:
	
  PublishingLink( const IPAddress & InitialRemoteEndpoint, 
							    const std::string & RemoteLinkServerName = "ZMQLink", 
							    const std::string & ServerName = "ZMQLink" );
	
	virtual ~PublishingLink( void )
	{ }
};

// -----------------------------------------------------------------------------
// Publisher
// -----------------------------------------------------------------------------
//

class Publisher : virtual public Actor, 
								  virtual public StandardFallbackHandler
{
private:
	
	// The last good value is a map from the topic value to a Serial Message 
	// pointer since the Serial Message is the base class of all messages that 
	// can be published. A shared pointer is used to ensure that the message 
	// copy it points to will be deleted on reassignment, or when the map is 
	// deleted by the class destructor.
	
	std::unordered_map< std::string, 
										  std::shared_ptr< SerialMessage > > LastGoodValue;
	
	// There is a handler for new subscriptions sent from the publishing link
	
	void NewSubscription( const PublishingLink::SendLastGoodValue & Broadcast, 
												const Address TheLink	);
	
	// The actor will simply call the publish function to send a message to all 
	// its subscribers. This will send the message to the publishing link and 
	// cache a copy of the message as the last good value for this topic.
	
protected:
	
	template< class MessageType >
	void Publish( const MessageType & TheMessage )
	{
		// All messages should be serial messages and copyable
		
		static_assert( std::is_base_of< SerialMessage, MessageType >::value,
			"The message to be published must be derived from the Serial Message"	);
		
		static_assert( std::is_copy_constructible< MessageType >::value, 
			"The message must be copy constructable"	);
		
		// The publishable message is created as it will provide the topic, and 
		// then sent to the publishing link to be transmitted to the outside.
		
		PublishingLink::PublishableMessage BroadcastMessage( 
																			 TheMessage, GetAddress().AsString() );
		
		Send( BroadcastMessage, Network::GetAddress( Network::Layer::Network ) );
		
		// Then the binary message can be stored as a copy in the Last Good Value 
		// cache
		
		LastGoodValue[ BroadcastMessage.Topic ] = 
																	std::make_shared< MessageType >( TheMessage );
	}
	
	// The constructor is protected to ensure that a publisher actor is not 
	// created stand-alone but only as a base class of a real actor using this 
	// interface.
	
	Publisher( const std::string Name = std::string() );
	
	virtual ~Publisher( void );
};

/*==============================================================================

 Subscribing

==============================================================================*/
//
// The subscriber actor is a utility base class for an actor that wants to 
// subscribe to events published by other actors. It provides a subscription 
// socket for receiving the data, and it connects to the data publishers on 
// the endpoints hosting the actors subscribed to. 
//
// The class maintains a cache of the last received message type for each topic,
// and if the received value is identical to the one already cached, it is 
// simply ignored. It should be noted that since the message format is encoded 
// in the topic string (see the publishing link above), an actor can subscribe 
// to many different message formats from the same publishing actor. The only 
// requirement is that the same message format cannot be used by two different 
// data streams from the same actor. 
//
// Since the messages are serialised it is mandatory that the message formats 
// used are derived from the Serial Message, and the subscriber actor has to 
// be a Deserializing actor in order to correctly convert the various serial 
// messages it receives. 

class Subscriber : virtual public Actor,
									 virtual public StandardFallbackHandler,
									 virtual public DeserializingActor
{
private:
	
	// The socket taking care of the subscription
	
	zmqpp::socket Subscription;

	// The subscribed topic must be unique, and in order to disconnect from the 
	// remote TCP endpoint when the last topic published by that endpoint is 
	// unsubscribed, the network address of the topic must be remembered. It is 
	// a philosophical question how to treat repeated updates with the same 
	// value on a topic. They could be seen as some kind of heartbeat signals 
	// that should be handled by the actor. However, they will be filtered out 
	// in this implementation since it is taken that the actual value is 
	// important and carries the information and not the arrival event. 
	// A heartbeat protocol must therefore ensure that the valued published must
	// be different from the last value (but it is OK to oscillate between two 
	// values). In order to do the value filtering the last received value, i.e.
	// the message payload, be stored.
	
	class TopicInformation
	{
	public:
		
		const NetworkAddress   Connection;
		SerialMessage::Payload LastReceivedValue;
		
		TopicInformation( const TCPAddress & TCPEndpoint, 
										  const std::string & ActorName = std::string() )
		: Connection( TCPEndpoint, ActorName ), LastReceivedValue()
		{ }
		
		TopicInformation( void ) = delete;
	};
	
	// The topics and the related information class are kept in an unordered map 
	
	std::unordered_map< std::string, TopicInformation > ActiveSubscriptions;
	
  // ---------------------------------------------------------------------------
  // Subscription handler
  // ---------------------------------------------------------------------------
	//
	// When the subscription socket has a message the socket monitor will call 
	// the handler for subscriptions

	void SubscriptionHandler( void );
	
  // ---------------------------------------------------------------------------
  // Subscribing and address resolution
  // ---------------------------------------------------------------------------
	//
  // To be able to connect the subscriber to the publisher, the publisher 
	// endpoint address must be known. The principle of transparent communication 
	// implies that an actor only needs to know the actor name of the remote actor
	// and not its location. It is the role of the session layer server to keep 
	// track of where agents are located, and the subscriber must use the address
	// resolution protocol to find the endpoint address of the actor.
	//
	// This implies that the subscriber must send a message to the network layer 
	// server and then wait for the response to come back as a separate message 
	// before the actual connection to the publisher socket can be made. During 
	// this interval, the actor can initiate subscriptions for other publishers,
	// and so it is important to cache the subscription requests and fulfil them 
	// once the corresponding endpoints become known. 
	
	std::multimap< Address, std::type_index > PendingRequests;
	
	// There is a message handler that will receive the resolved address from 
	// the session layer server and then set the topic and subscribe. The pending
	// request is deleted after connecting the socket to the publisher for the 
	// actor.
	
  void ConnectSubscription( const Link::ResolutionResponse & ResolvedAddress, 
														const Address SessionLayerServer );

	// The main method for the actor itself is the subscribe method that stores
	// the request as pending, and asks the network layer server to resolve the 
	// actor's external address. Note that this must be called with template 
	// syntax making the message type explicit.
	
protected:
	
	template< class MessageType >
	void Subscribe( const Address & PublishingActor )
	{
		static_assert( std::is_base_of< SerialMessage, MessageType >::value,
			"It is only possible to subscribe to messages derived from Serial Message"
		);
		
		PendingRequests.emplace( PublishingActor, typeid( MessageType ) );
		
		Send( Link::ResolutionRequest( PublishingActor ), 
					Network::GetAddress( Network::Layer::Network ) );
	}
	
	// The second version of this function assumes that both the topic and the 
	// network endpoint is known. A message type must still be given to verify 
	// that the received payload will be de-serialised by a message, and the 
	// message will be sent with "this" actor as sender (sending-to-self) since 
	// the remote publisher may not even be an actor. For the same reason, this 
	// topic will not be registered in the map of received values, since the 
	// actual notification could be a heartbeat from the remote publisher. It 
	// should be noted in passing that the time interval between two heartbeats
	// cannot be taken as a constant.
	
	template< class MessageType >
	void Subscribe( const std::string & Topic, const TCPAddress & Location, 
									const std::string & PublishingActorName = std::string() )
	{
		static_assert( std::is_base_of< SerialMessage, MessageType >::value,
			"A Serial Message must receive the serialised payload subscribed to"
		);
		
		// The topic is inserted into the list of active connections, and 
		// connected if the topic is unique.
		
		auto Result = ActiveSubscriptions.emplace( Topic, Location, 
																							 PublishingActorName );
		
		if ( Result.second == true )
		{
			// It is assumed that it will not harm if a second connection for the same 
			// TCP endpoint is made. This could happen if a one subscribes to a 
			// different topic for the same endpoint.
			
			Subscription.connect( Location.AsString() );		
			Subscription.subscribe( Topic );
		}
		else
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << "Subscription topic \"" << Topic << "\" does already "
									 << "exists for actor " << GetAddress().AsString();
									 
		  throw std::invalid_argument( ErrorMessage.str() );
		}
	}

	// It is also possible to unsubscribe from a subscription. In this case
	// the message type has no meaning, but is required for aligning the syntax 
	// of the functions. If there are no active topics for the given endpoint,
	// then the subscription socket will be disconnected from the endpoint.   
	
  template< class MessageType >
  void Unsubscribe( const std::string & Topic )
	{
		// First a sanity check to see if there is a connection to unsubcribe from
		
		auto CurrentSubscription = ActiveSubscriptions.find( Topic );
		
		if ( CurrentSubscription != ActiveSubscriptions.end() )
		{
			Subscription.unsubscribe( Topic );
			
			// Then all connections must be checked to see if there are other 
			// connections sharing the same TCP endpoint. The current subscription 
			// will anyway be removed, but its network address should be cached 
			// before removal.
			
			std::string PublisherAddress = 
									CurrentSubscription->second.Connection.GetEndpoint();
			
			ActiveSubscriptions.erase( CurrentSubscription );
			
			// Now the current subscription iterator can be used to check if there 
			// are other connections to the same remote endpoint
			
			for ( CurrentSubscription = ActiveSubscriptions.begin(); 
					  CurrentSubscription != ActiveSubscriptions.end(); 
						++CurrentSubscription )
			  if( PublisherAddress == 
					  CurrentSubscription->second.Connection.GetEndpoint() )
					return;
				
			// Arriving at this point means that this was the only connection to 
			// this TCP endpoint and it will be disconnected
			
			Subscription.disconnect( PublisherAddress );
		}
	}
		
	// It is also possible to unsubscribe from a topic. The format of the method
	// is the same, but the action is immediate. This means that the topic string 
	// must be parsed: It is necessary to find the actor publishing and then the 
	// message type being published. If both matches, it is possible to
	// unsubscribe and forget about the topic. Since the filtering happens at 
	// the publisher side since ZMQ 3.0 it is safe to assume that the socket 
	// communicates the removed topic to all connected publishers and the one 
	// serving this topic notifies that this endpoint will no longer receive 
	// updates.
	
	template< class MessageType >
	void Unsubscribe( const Address & PublishingActor )
	{
		// It is not strictly necessary here to ensure that the message is a serial
		// message, but it adds to the compile time checking of possible mistakes
		
		static_assert( std::is_base_of< SerialMessage, MessageType >::value, 
			"Only Serial Messages can be subscribed to"	);
		
		// Since this is a managed subscription, the format of the topic string 
		// is given and the topic based unsubcribe function can be used.
		
		Unsubscribe< MessageType >( 
				PublishingLink::SetTopic< MessageType >( PublishingActor.AsString() ) );
	}

  // ---------------------------------------------------------------------------
  // Constructor
  // ---------------------------------------------------------------------------
	//
	// The constructor optionally takes a name for the actor. It should be noted 
	// that this constructor is protected (on purpose) to ensure that no free 
	// subscriber object is instantiated. It must be used by a proper actor that 
	// knows how to handle the payload content of the received events.
	
	Subscriber( const std::string & name = std::string() );
	
	// The virtual destructor unsubscribes from all active connections and 
	// disconnect from all remote endpoints before the monitor is requested to 
	// stop monitoring this socket. 

public:
	
	virtual ~Subscriber( void );
};

}					// name space Theron::ZeroMQ
#endif 		// THERON_ZERO_MESSAGE_QUEUE
