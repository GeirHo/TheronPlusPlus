/*=============================================================================
Zero Message Queue

This file implements the Zero Message Queue interface for Theron++. Please see
the corresponding header for details, and the references.

Author and Copyright: Geir Horn, 2017
License: LGPL 3.0
=============================================================================*/

#include <map>						              // To match types and strings
#include <list>                         // To store lists of IP addresses
#include <set>                          // To store unique active connections
#include <algorithm>                    // To search for strings
#include <cstdio>												// For sscanf
#include <stdexcept>										// Standard exceptions
#include <sstream>											// For error reporting

#include <sys/types.h>									// Types used for IP address lookup
#include <ifaddrs.h>                    // The getifaddrs function
//#include <netinet/in.h> 
#include <arpa/inet.h>                  // Converting IP info to string

#include <boost/system/error_code.hpp>  // Boost error reporting
#include <boost/algorithm/string.hpp> 	// To convert to lower-case 

#include "ZeroMQ.hpp"                   // ZMQ interface definition

/*=============================================================================

 Get the IP address of the computer

=============================================================================*/
//
// When the link layer starts up it needs to bind the local ports to the 
// sockets and then broadcast these addresses to its fellow peers, an new peers
// as they become available. Finding the own IP address is a non-trivial task,
// and the following implementation is a mix of information from several 
// sources on how to use the getifaddrs function and interpret the results.
// The manual page [11] gives the basic information but it does not document 
// how to convert the obtained results to a useful form. The best example was 
// provided by Twelve42 in a Stack Exchange response [12] that also shows how 
// to use the inet_ntop [13] function to convert the address to a useful string.
//
// The following function returns a list of IP addresses in the order returned 
// by the getifaddrs function. 

std::list< std::pair< std::string, Theron::ZeroMQ::IPAddress > >
InterfaceIP( void )
{
	struct ifaddrs * LookupResult = nullptr;
	
	// The lookup is performed on the socket needed for publishing to other 
	// peers. It does not specify the local host name as it is unknown
	
	if ( getifaddrs( &LookupResult ) == -1 )
  {
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << " : "
								 << "Failed to look up the IP address of the network "
								 << "interfaces: " << strerror( errno );
								 
		if ( LookupResult != nullptr )
			freeifaddrs( LookupResult );
								 
	  throw std::runtime_error( ErrorMessage.str() );
	}
	else
  {
		// There were some results, and the returned addresses can be parsed one 
		// by one and added to the priority list of interfaces.
		
		std::list< std::pair< std::string, Theron::ZeroMQ::IPAddress > > Interfaces;
		
		for ( struct ifaddrs * Interface = LookupResult; Interface != nullptr;
			    Interface = Interface->ifa_next )
		  if ( Interface->ifa_addr != nullptr )
			{
				// The string addresses will be stored in a buffer allocated to the size 
				// of an IPv6 address since that is longer than the IPv4 address.
				
				char IPString[ INET6_ADDRSTRLEN ];
				
				// The processing is dependent on the IP protocol versions since the 
				// involved structures are different in the two cases.
				
				if ( Interface->ifa_addr->sa_family == AF_INET ) // IPv4 address
				  inet_ntop( AF_INET, 
										 & ((struct sockaddr_in *)Interface->ifa_addr)->sin_addr, 
										 IPString, INET6_ADDRSTRLEN );
				else // This is an IPv6 address
					inet_ntop( AF_INET6, 
										 & ((struct sockaddr_in6 *)Interface->ifa_addr)->sin6_addr, 
										 IPString, INET6_ADDRSTRLEN );
				
				Interfaces.emplace_back( Interface->ifa_name, 
													 Theron::ZeroMQ::IPAddress::from_string( IPString ));
			}
		
		freeifaddrs( LookupResult );
		return Interfaces;
	}
}

/*=============================================================================

 Converting and transmitting messages

=============================================================================*/
//
// Constructing a TCP address from an IP string is involves checking that the 
// string was really an IP address that could be correctly decoded.

Theron::ZeroMQ::TCPAddress::TCPAddress( const std::string & TheIP, 
																				unsigned short ThePort     )
{
	// The port number can be stored directly
	
	PortNumber = ThePort;
	
  // If the IP string is given as * it means that the address is for this 
	// network endpoint and the preferred IP interface for the given port 
	// will be returned by the IP lookup function. Otherwise, the given string 
	// is supposed to contain a valid IP address.
	
	if ( TheIP == "*" )
  {
		auto LocalInterfaces = InterfaceIP();
		
		if ( LocalInterfaces.empty() )
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << " : "
									 << "No suitable network interface could be found on "
									 << "this computer";
									 
		  throw std::runtime_error( ErrorMessage.str() );
		}
		else
		{
			// There are useful interfaces, and it is necessary to select one 
			// interface. IPv6 is unfortunately not as widespread as IPv4 
			// addresses, and so the IPv6 addresses are discarded.
									
			LocalInterfaces.remove_if( 
			[]( const std::pair< std::string, Theron::ZeroMQ::IPAddress > & 
					Interface )->bool{ return Interface.second.is_v6(); } );
			
			// Then the loop back interface is removed. However, since the name of 
			// the loop back interface is unknown, and it is necessary to seek 
			// for the name first.
			
			auto LoopBackPointer = std::find_if( 
				LocalInterfaces.begin(), LocalInterfaces.end(),
				[]( const std::pair< std::string, Theron::ZeroMQ::IPAddress > & 
				    Interface )->bool{ 
							return Interface.second.to_string() == "127.0.0.1"; });
			
			if ( LoopBackPointer != LocalInterfaces.end() )
		  {
				std::string LoopBackName( LoopBackPointer->first );
				
				LocalInterfaces.remove_if( 
				[&]( const std::pair< std::string, Theron::ZeroMQ::IPAddress > & 
					Interface )->bool{ return Interface.first == LoopBackName; }  );
			}
			
			// Finally, the remaining interfaces can be sorted in ascending order
			// based on the interface name. At least for Linux this this will put 
			// the wired interfaces (ethN) first and then the wireless interfaces 
			// last.
			
			LocalInterfaces.sort(
				[]( const std::pair< std::string, Theron::ZeroMQ::IPAddress > & A,
					  const std::pair< std::string, Theron::ZeroMQ::IPAddress > & B
				  )->bool{ return A.first < B.first; } );
			
			// It should now be sufficient to pick the first interface in the list.
			
			IP = LocalInterfaces.front().second;			
		}
	}
	else
  {
		// The string did not contain * and must therefore represent a 
		// syntactically valid IP address. 
		
		boost::system::error_code IPConversion;

		IP  = IPAddress::from_string( TheIP, IPConversion );
		
		if ( IPConversion.value() != boost::system::errc::success )
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "Failed to convert the given IP address string "
									 << TheIP << " (" << IPConversion.message() << ")";
									 
		  throw std::invalid_argument( ErrorMessage.str() );
		}			
	}
}

// Parsing the address when it is given only as a string is not so easy.
// The first is a check that a string is really a representation of a TCP 
// address. This is done by the address object constructor when it receives a 
// string argument. The format of the string should be 
// "tcp://<IP string>:<Port number>"
// and it will throw a standard invalid argument expression if the parts of 
// this string could not be correctly read, or if the parsing of the IP string 
// fails. 
// 
// Parsing the string is not trivial and sscanf fails to read the string in a 
// generic way without encoding the IP version in the scan format. The string 
// is therefore parsed directly.

Theron::ZeroMQ::TCPAddress::TCPAddress( const std::string & TheAddress )
{
	// First the cosmetic ensuring that the string is in lower case before 
	// starting to parse the string.
	
	std::string AddressString( TheAddress );
	
	boost::to_lower( AddressString );
	
	// It is then possible to assert that the string contains the required 
	// protocol definition "tcp://" and if it does, it will be dropped from 
	// the string.
	
	std::string::size_type Position = AddressString.find( "tcp://" );
	
	if ( Position == std::string::npos )
  {
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << "No TCP protocol the address string \"" 
								 << AddressString << "\" that should have been \""
								 << "tcp://<IP address>:<Port Number>\"";
								 
	  throw std::invalid_argument( ErrorMessage.str() );
	}
	else
		AddressString.erase( 0, 6 );
	
	// Then the port number part is identified by finding the first colon in 
	// the string starting from the end.
	
	Position = AddressString.find_last_of(':');
	
	if ( Position == std::string::npos )
  {
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << "No port number in the TCP address string \"" 
								 << AddressString << "\" that should have been \""
								 << "tcp://<IP address>:<Port Number>\"";
								 
	  throw std::invalid_argument( ErrorMessage.str() );		
	}
	else
  {
		// The port number is read from the string after the found colon. Note 
		// that this may throw an invalid argument exception if the conversion 
		// fails. If successful then the port part including the colon is removed
		
		PortNumber  = std::stoi( AddressString.substr( Position+1 ) );
		AddressString.erase( Position );
	}
	
	// At this point the address string should contain only the IP address. 
	// However, if it is an IPv6 address it could be enclosed in brackets to 
	// avoid confusion between the colon of the address and the colon separating 
	// the port number, see [10]. Hence if the first character is a bracket, 
	// both the first and the last character will be removed.
	
	if ( AddressString[0] == '[' )
  {
		AddressString.erase( 0, 1 );
		AddressString.erase( AddressString.size()-1, 1 );
	}	
	
	// We did get something seeming to be in the right format, so we can 
	// proceed to decode the IP address, delegating to the constructor for 
	// IP address strings. Since it cannot be called directly, a temporary 
	// object must be constructed. If the construction does not fail, then the 
	// values of the temporary object can be assigned to this TCP address.
 
	TCPAddress TestAddress( AddressString, PortNumber );
	*this = TestAddress;
}

// -----------------------------------------------------------------------------
// Transmitting the network address
// -----------------------------------------------------------------------------
//
// The network address is inserted into the ZMQ message as one frame by 
// overloading the stream operator. 

zmqpp::message & operator << (zmqpp::message & MessageToSend, 
											        const Theron::ZeroMQ::NetworkAddress & TheAddress)
{
	MessageToSend << TheAddress.GetEndpoint() + " " + TheAddress.GetActorName();
	
	return MessageToSend;
}

// In the same way it can be extracted from a message frame

zmqpp::message & operator >> ( zmqpp::message & MessageReceived, 
			 										     Theron::ZeroMQ::NetworkAddress & TheAddress )
{
	std::string MessageFrame, TheEndpoint, TheActor;
		
	MessageReceived >> MessageFrame;

	std::istringstream AddressFrame( MessageFrame );
	
	AddressFrame >> TheEndpoint >> std::ws >> TheActor;
	
	TheAddress = Theron::ZeroMQ::NetworkAddress( TheEndpoint, TheActor );
	
	return MessageReceived;
}

// -----------------------------------------------------------------------------
// Transmitting the outside messages
// -----------------------------------------------------------------------------
//
// First the map between the types of the outside message and the string 
// representing the message type is defined as a constant map.

const std::map< std::string, Theron::ZeroMQ::OutsideMessage::Type > TypeMap
= { { "Subscribe", Theron::ZeroMQ::OutsideMessage::Type::Subscribe },
	  { "Address",   Theron::ZeroMQ::OutsideMessage::Type::Address   },
		{ "Remove",    Theron::ZeroMQ::OutsideMessage::Type::Remove    },
		{ "Roster",    Theron::ZeroMQ::OutsideMessage::Type::Roster    },
		{ "Message",   Theron::ZeroMQ::OutsideMessage::Type::Message   },
		{ "Data",      Theron::ZeroMQ::OutsideMessage::Type::Data      } };
		
using TypeRecord = 
			std::pair< std::string, Theron::ZeroMQ::OutsideMessage::Type >;

// The utility function doing the reverse lookup is implemented as a search 
// for the right binary type in the second field of the map. This has linear 
// complexity, but the comparison is simpler than comparing strings, and the 
// number of possible commands is not too many. However, one may use a second 
// map defined with the typed enumerations as key at the expense of using more 
// memory and having to maintain the two maps consistently; or even Boost 
// bimap although this possibly comes with a complexity overhead too.
			
std::string Theron::ZeroMQ::OutsideMessage::Type2String( void ) const
{
	auto TheType = std::find_if( TypeMap.begin(), TypeMap.end(), 
													     [this]( const TypeRecord & Command )->bool{
																	 return Command.second == MessageType; });
	
	// It should be noted that since this is done for a message the type must 
	// exist and have a legal value, and the find will succeed. Hence it is just
	// to return the result of the lookup.
	
	return TheType->first;
}

// Converting an incoming message to the outside message format is relatively 
// easy given that each ZMQ message field corresponds to the parts of the 
// outside message. The fields are laid out in the following order:
//	1. Type (or data in case of message received from a different sensor)
//  2. From address
//  3. To address
//  4. Payload (optional if command message)
		
Theron::ZeroMQ::OutsideMessage::OutsideMessage(zmqpp::message & ReceivedMessage)
{
	std::string MessageField;
	
	ReceivedMessage >> MessageField;
	
	auto Type = TypeMap.find( MessageField );
	
	// If the given field was not recognised as a valid command, it should be 
	// treated as the payload and the type should be set to Data. The sender and
	// receiver must be defined by another mechanism.
	
	if ( Type == TypeMap.end() )
  {
		MessageType = Type::Data;
		Payload     = MessageField;
	}
	else
  {
		MessageType = Type->second;
		ReceivedMessage >> SenderAddress >> ReceiverAddress >> Payload;
	}
}

// Converting the outside message to a set of ZMQ frames is slightly easier 
// since all fields does exist in this case. 

void Theron::ZeroMQ::OutsideMessage::Send( zmqpp::socket & NetworkSocket ) const
{
	zmqpp::message NetworkMessage;
	
	NetworkMessage << Type2String() << SenderAddress << ReceiverAddress 
								 << Payload;
	
	NetworkSocket.send( NetworkMessage );
}

/*=============================================================================

 Peer management

=============================================================================*/
//
// A new peer means another ZeroMQ link server hosted on a remote node serving
// that network endpoint.  

void Theron::ZeroMQ::Link::AddNewPeer( const IPAddress & PeerIP )
{
	// First the subscriber socket is connected to the peer's publisher socket
	
	Subscriber.connect( TCPAddress( PeerIP, PublisherPort ).AsString() );
	
	// Then an outbound socket is created and connected to the inbound port of 
	// the remote peer.
	
	Outbound.emplace( PeerIP,
										zmqpp::socket( ZMQContext, zmqpp::socket_type::dealer ) );
	
	Outbound.at( PeerIP ).connect( TCPAddress( PeerIP, InboundPort ).AsString() );
}

/*=============================================================================

 Actor address management

=============================================================================*/
//
// If the sender is an actor on the local endpoint, the external address is 
// returned as the TCP endpoint corresponding to the message port. If the 
// request is for a remote actor things get slightly more complicated however. 
// Then an address request is constructed and published on the broadcast 
// socket.

void Theron::ZeroMQ::Link::ResolveAddress( const ResolutionRequest & TheRequest, 
																				   const Address TheSessionLayer )
{
	if ( TheRequest.NewActor.IsLocalActor() )
		Send( ResolutionResponse( NetworkAddress( OwnAddress.GetEndpointLocation(), 
																							TheRequest.NewActor ), 
															TheRequest.NewActor ), TheSessionLayer );
	else
  {
		// The request is sent as if it was from the "session layer" which normally
		// corresponds to the session layer, but it could be any other actor 
		// knowing about this resolution protocol. 
		
		OutsideMessage AddressRequest( OutsideMessage::Type::Address, 
																   NetworkAddress( 
																		   OwnAddress.GetEndpointLocation(),
																			 TheSessionLayer ),
																	 NetworkAddress( 
																			 TCPAddress(), 
																			 TheRequest.NewActor ) );
		
		AddressRequest.Send( Publish );
	}
}

// When a local actor is closing the removal is broadcast to all the other 
// actors. This because there can be actors on remote nodes that have sent 
// messages to this actor, and the session layer needs to be informed that it 
// is no longer existing for doing the correct housekeeping.

void Theron::ZeroMQ::Link::ActorRemoval( const RemoveActor & TheCommand, 
																				 const Address TheSessionLayer  )
{
	OutsideMessage RemovalCommand( OutsideMessage::Type::Remove, 
																 TheCommand.GlobalAddress, NetworkAddress() );
	
	RemovalCommand.Send( Publish );
}

// If an request for an address resolution comes in on the subscriber, it will 
// forward a local actor inquiry to the local session layer. If the actor does 
// exist on this endpoint, the session layer will return the inquiry message 
// with the address of the actor set equal to the stored external address of 
// the actor.

void Theron::ZeroMQ::SessionLayer::CheckActor( 
		 const Link::LocalActorInquiry & TheRequest, const Address TheLinkServer )
{
	Address TheActor( TheRequest.ActorRequested.GetActorAddress() );
	
	if ( TheActor.IsLocalActor() )
  {
		auto ActorRecord = KnownActors.right.find( TheActor );
		
		if ( ActorRecord != KnownActors.right.end() )
			Send( Link::LocalActorInquiry( ActorRecord->second, 
																		 TheRequest.RequestingEndpoint ), 
					  TheLinkServer );
	}
}

// When this inquiry message comes back to the link, it will be sent back to 
// the requesting endpoint using the outbound socket for that endpoint. When 
// this message arrives at the remote endpoint it will be stored by the session
// layer server for that endpoint, and then all messages cached for the actor 
// on this endpoint will be sent.

void Theron::ZeroMQ::Link::FoundLocalActor(
	const LocalActorInquiry & TheResponse, const Address TheSessionLayer)
{
	OutsideMessage TheReply( OutsideMessage::Type::Address, 
													 TheResponse.ActorRequested, 
													 TheResponse.RequestingEndpoint );
	
	TheReply.Send( Outbound.at( TheResponse.RequestingEndpoint.GetIP() ) );
}


/*=============================================================================

 Link Message handling

=============================================================================*/
//
// The outbound message handler receives an outside message from the session 
// layer, and forwards the message to the right receiver endpoint based on 
// the message type. All messages should be messages since the command messages
// are handled by other dedicated functions.

void Theron::ZeroMQ::Link::OutboundMessage( const OutsideMessage & TheMessage, 
																						const Address From)
{
	switch ( TheMessage.GetType() )
	{
		case OutsideMessage::Type::Message : 
			TheMessage.Send( Outbound.at( TheMessage.GetRecipient().GetIP() ) );
			break;
		default:
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "ZeroMQ::Link message with illegal type "
									 << TheMessage.Type2String() << " from sender " 
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

void Theron::ZeroMQ::Link::InboundMessageHandler( void )
{
	std::string    DevNull;
	zmqpp::message ReceivedMessage;
	
	Inbound.receive( ReceivedMessage );
	ReceivedMessage >> DevNull;
	
	OutsideMessage TheMessage( ReceivedMessage );
	
	switch ( TheMessage.GetType() )
  {
		case OutsideMessage::Type::Address :
			// An request for address resolution has been completed with the endpoint
			// hosting the actor responding with the actor's address that is indicated
			// as the sender of the message. It will be sent to the session layer 
			// as a resolution response message.
			
			Send( ResolutionResponse( TheMessage.GetSender(), 
																TheMessage.GetSender().GetActorAddress() ), 
																TheMessage.GetRecipient().GetActorAddress() ); 
			
			break;
		case OutsideMessage::Type::Message :
			// A normal message is just forwarded to the session server so that the 
			// local actor can be identified and the message forwarded to that actor.
			
			Send( TheMessage, 
						Network::GetAddress( Network::Layer::Session ) );
			
			break;
		case OutsideMessage::Type::Subscribe :
		{
			// First notify all the connected peers about this new peer available
			// by asking them to connect to the new peer.
			
			OutsideMessage NewPeerNotification( OutsideMessage::Type::Subscribe, 
																					NetworkAddress(), 
																					TheMessage.GetSender() );
			
			NewPeerNotification.Send( Publish );
			
			// The next is to send all the peers known to us back to the new peer so 
			// that it can connect to them all. The roster string is constructed 
			// first to avoid the inclusion of the new peer we will connect to.
			
			std::ostringstream Roster;
			
			for ( auto & KnownPeer : Outbound )
				Roster << KnownPeer.first.to_string() << " ";
			
			// Add and connect the new peer
			
			IPAddress NewPeer( TheMessage.GetSender().GetIP() );
			AddNewPeer( NewPeer );
			
			// Create and send the roster message
			
			OutsideMessage RosterMessage( OutsideMessage::Type::Roster,
																		OwnAddress, TheMessage.GetSender(), 
																		Roster.str() );
			
			RosterMessage.Send( Outbound.at( NewPeer ) );
			break;
		}
		case OutsideMessage::Type::Roster :
		{
			// The IPs of the peers are read off the message payload and added 
			// as new peers in the system.
			
			std::istringstream RosterString( TheMessage.GetPayload() );
			
			while ( !RosterString.eof() )
			{
				std::string PeerIP;
				
				RosterString >> PeerIP >> std::ws;
				AddNewPeer( IPAddress::from_string( PeerIP ) );
			}
			break;
		}
		default:
			// The message is just forgotten. A data message should not be received 
			// on this inbound port.
			break;
	}
}

// The hander for the subscribe socket can receive two kinds of messages: The 
// subscribe message indicating a new peer available, and an address message 
// from another peer wanting to know which endpoints that is hosting a named 
// actor.

void Theron::ZeroMQ::Link::SubscribedMessageHander( void )
{
	zmqpp::message ReceivedMessage;
	
	Subscribe.receive( ReceivedMessage );
	
	OutsideMessage TheMessage( ReceivedMessage );
	
	switch ( TheMessage.GetType() )
  {
		case OutsideMessage::Type::Address :
			// An address message is fundamentally a question to the session layer 
			// server if the named actor is hosted on this network endpoint. Hence, 
			// the message is forwarded to the session layer as a local actor 
			// inquiry.
			
			Send( LocalActorInquiry( TheMessage.GetRecipient(), 
															 TheMessage.GetSender() ), 
						Network::GetAddress( Network::Layer::Session ) );
			
			break;
		case OutsideMessage::Type::Remove :
			Send( RemoveActor( TheMessage.GetSender() ), 
						Network::GetAddress( Network::Layer::Session ) );
			break;
		case OutsideMessage::Type::Subscribe :
			AddNewPeer( TheMessage.GetSender().GetIP() );
			break;
		default:
			// There should not be any other message type...
			break;
	}
}

/*=============================================================================

 Link Constructor

=============================================================================*/
//
// Some of the variables are static because it is a need for the data 
// publishing derived link class to offer a function to publish messages 
// that can be called without having a reference to the link server object. 
// Actors subscribing to published events will need a subscriber context 
// attached to the network endpoint's context and monitored by the network 
// endpoint's reactor. The address of the current endpoint should also be 
// static in order to set topics for event subscriptions.

zmqpp::context                 Theron::ZeroMQ::Link::ZMQContext;
zmqpp::reactor                 Theron::ZeroMQ::Link::SocketMonitor;
Theron::ZeroMQ::NetworkAddress Theron::ZeroMQ::Link::OwnAddress;
 
// The constructor initialises the various sockets and starts the connection 
// to the other peer endpoints of the actor system if this is not the first 
// endpoint indicated by having the local host as IP address.

Theron::ZeroMQ::Link::Link( const IPAddress & InitialRemoteEndpoint, 
														const std::string & RemoteLinkServerName, 
														const std::string & ServerName )
: Actor( ServerName ), StandardFallbackHandler( GetAddress().AsString() ),
  NetworkLayer< OutsideMessage >( GetAddress().AsString() ),
  Publish   ( ZMQContext, zmqpp::socket_type::publish   ),
  Subscribe ( ZMQContext, zmqpp::socket_type::subscribe ),
  Inbound   ( ZMQContext, zmqpp::socket_type::router    ),
  Outbound()
{
	// The global network address of this endpoint is fixed first.
	
	OwnAddress = NetworkAddress( TCPAddress("*", InboundPort ), ServerName );
	
	// The sockets accepting connections are bound to the ports - note that by 
	// using the wildcard notation for the IP it means that they will listen for 
	// connections on any network interface of the host computer. This is useful 
	// if the host computer has more than one network interface. Furthermore, 
	// the own address constructed above will contain the IP address of only 
	// one of these these adapters, and this IP address may not be the same IP 
	// address as returned from a DNS lookup for this machine. On the other hand, 
	// the own address cannot be set to the address of the machine since there 
	// may not be a useful DNS registration for this machine (if the IP is 
	// dynamically assigned by DHCP its DNS ID may just be the IP with a 
	// temporary flag. It will therefore return a DNS lookup, but this address 
	// is as valuable as having the raw IP address.)
	
	Publish.bind( std::string( "tcp://*:" ) + std::to_string( PublisherPort ) );
	Inbound.bind( std::string( "tcp://*:" ) + std::to_string( InboundPort   ) );
	
	// Then the receiving sockets are added to the socket monitor ensuring that 
	// the right handler will be invoked when they get a message. Lambda functions
	// must be used since the member functions cannot be used as function pointers
	// without the 'this' pointer or an object for which the pointer can be called
	
	SocketMonitor.add( Subscribe, 
										 [this](void)->void{ SubscribedMessageHander(); } );
	
	SocketMonitor.add( Inbound, [this](void)->void{ InboundMessageHandler(); } );
	
	// The first frame received on the subscriber socket is the "topic" of the 
	// published message. This corresponds to the message type of the outbound 
	// message, and the socket subscribes to these topics. It is necessary to 
	// construct a temporary outside since the function converting the type to 
	// a string converts the type of the message it is called for.

	Subscribe.subscribe( OutsideMessage( OutsideMessage::Type::Address, 
											 								 OwnAddress, OwnAddress ).Type2String());
	Subscribe.subscribe( OutsideMessage( OutsideMessage::Type::Remove, 
											 								 OwnAddress, OwnAddress ).Type2String());	
	Subscribe.subscribe( OutsideMessage( OutsideMessage::Type::Subscribe, 
											 								 OwnAddress, OwnAddress ).Type2String());
	
	// This link server should connect to the initial remote peer if it is not 
	// the local host, which would be the case if it is the first peer to start.
	
	if ( InitialRemoteEndpoint != LocalHost() )
  {
	  AddNewPeer( InitialRemoteEndpoint );
		
		// An initial subscribe request is then sent to the remote peer with the 
		// IP of this endpoint as argument to trigger the initialisation and the 
		// discovery of the other peers in the system. 
		
		OutsideMessage SubcriptionRequest( 
		    OutsideMessage::Type::Subscribe, OwnAddress, 
				NetworkAddress( TCPAddress( InitialRemoteEndpoint, InboundPort ), 
												RemoteLinkServerName ),
				OwnAddress.GetIP().to_string() );
		
		SubcriptionRequest.Send( Outbound.at( InitialRemoteEndpoint ) );
	}
	
	// Finally the handler for resolved address resolution requests is registered
	
	RegisterHandler( this, &Link::FoundLocalActor );
}

/*==============================================================================

 Publishing

==============================================================================*/
//
// When a subscriber wants to subscribe or unsubscribe the event handler is 
// called. Only subscriptions are processed for now, and if the topic 
// wanted by the subscriber is in the cache, it will be published again trusting
// that other subscribers are able to filter out this double posting.

void Theron::ZeroMQ::PublishingLink::SubscriptionEvents( void )
{
	enum class EventType : unsigned short int
	{
		Unsubscribe = 0,
		Subscribe   = 1
	};
	
	// Read the event from the socket
	
	zmqpp::message TheEvent;
	DataPublisher.receive( TheEvent );
	
	// Parsing the event
	
	unsigned short int Command;
	std::string EventString, Topic;
	
	TheEvent >> EventString;
	std::istringstream EventRecord( EventString );
	
	EventRecord >> Command >> std::ws >> Topic;
	
	// Then check the cache and re-publish the last known value if this was a 
	// subscription. If the topic is unknown, nothing will be done by this node
	// as the subscription was probably made before any actor published on this 
	// topic so the subscriber just have to wait.
	
	if ( Command == static_cast< unsigned short int>( EventType::Subscribe ) )
  {
		auto ThePublisher = Publishers.find( Topic );
		
		if ( ThePublisher != Publishers.end() )
			Send( SendLastGoodValue( Topic ), ThePublisher->second );
	}
}

// -----------------------------------------------------------------------------
// Message handlers
// -----------------------------------------------------------------------------
//
// When the publisher receives the command to send the cached value, it will 
// check if there is a value for this topic, and if so, send it as a normal 
// publishable message to be dispatched by the publishing link.

void Theron::ZeroMQ::Publisher::NewSubscription(
	const PublishingLink::SendLastGoodValue & Broadcast, const Address TheLink)
{
	auto ValueRecord = LastGoodValue.find( Broadcast.Topic );
	
	if ( ValueRecord != LastGoodValue.end() )
		Send( PublishingLink::PublishableMessage( Broadcast.Topic, 
																							ValueRecord->second ), TheLink );
}

// Messages will come from actors in terms of a message that already contains
// the serialised payload.

void Theron::ZeroMQ::PublishingLink::DispatchMessage(
		 const PublishableMessage & TheMessage, const Address ThePublisher )
{
	// Publishing the message
	
	zmqpp::message LinkMessage;
	
	LinkMessage << TheMessage.Topic << TheMessage.Payload;
	DataPublisher.send( LinkMessage );
	
	// Then storing the address of the actor owning this topic. If the topic is 
	// known already the actor address will be overwritten by itself, but this 
	// is probably as efficient as testing for this case.
	
	Publishers[ TheMessage.Topic ] = ThePublisher;
}

// When a publisher closes, all its topics should be deleted. Given that the 
// unordered map keeps the relative order of the elements when an element is 
// deleted, the lookup on the value field, i.e. the address of the publisher as
// the second field of an element in the map, can be done in one iteration.

void Theron::ZeroMQ::PublishingLink::DeletePublisher(
	const ClosePublisher & TheCommand, const Address ThePublisher )
{
	auto PublisherRecord = Publishers.begin();
	
	while ( PublisherRecord != Publishers.end() )
		if( PublisherRecord->second == ThePublisher )
			PublisherRecord = Publishers.erase( PublisherRecord );
		else
			++PublisherRecord;
}

// -----------------------------------------------------------------------------
// Constructors and destructor
// -----------------------------------------------------------------------------
//

Theron::ZeroMQ::PublishingLink::PublishingLink( 
	const IPAddress & InitialRemoteEndpoint, 
	const std::string & RemoteLinkServerName, const std::string & ServerName)
: Actor( ServerName ), StandardFallbackHandler( GetAddress().AsString() ),
  Link( InitialRemoteEndpoint, RemoteLinkServerName, GetAddress().AsString() ),
  DataPublisher( GetContext(), zmqpp::socket_type::xpublish )
{
	DataPublisher.set( zmqpp::socket_option::xpub_verbose, true );
	DataPublisher.bind( TCPAddress( GetNetworkAddress().GetIP(), 
																	DataPort ).AsString() );
	GetMonitor().add( DataPublisher, 
										[this](void)->void{ SubscriptionEvents(); } );
	
	RegisterHandler( this, &PublishingLink::DispatchMessage );
	RegisterHandler( this, &PublishingLink::DeletePublisher );
}

// The publisher's constructor simply initialises the cache and register the 
// hander for new subscriptions.

Theron::ZeroMQ::Publisher::Publisher(const std::string Name)
: Actor( Name ), StandardFallbackHandler( GetAddress().AsString() ),
  LastGoodValue()
{
	RegisterHandler( this, &Publisher::NewSubscription );
}

// The publisher's destructor must inform the publishing link that it closes

Theron::ZeroMQ::Publisher::~Publisher()
{
	Send( PublishingLink::ClosePublisher(), 
				Network::GetAddress( Network::Layer::Network ) );
}

/*==============================================================================

 Subscriber actor

==============================================================================*/
//
// When a subscription is made for a message from an actor, a request for
// finding the network endpoint will be sent to the local network layer server,
// which may optionally involve remote network layer servers and their session
// layer servers to find the publishing actor's location. The response from 
// the link layer server at the network endpoint hosting the actor will be 
// returned as a resolution response message. 
//
// The handler for this message type will then connect to the endpoint that 
// has the publishing actor, and then subscribe to all the topics corresponding 
// to different message types that local actors subscribe to.

void Theron::ZeroMQ::Subscriber::ConnectSubscription( 
		 const Link::ResolutionResponse & ResolvedAddress, 
		 const Address SessionLayerServer)
{
	Subscription.connect( ResolvedAddress.GlobalAddress.GetEndpoint() );
	
	// Find the range of topics pending for the publishing actor and subscribe to
	// these topics.
	
	auto Range = PendingRequests.equal_range( ResolvedAddress.TheActor );
	
	// if there are elements in the range, the actor name and the endpoint ID 
	// string are cached as they will be needed for each message type subscribed
	// to from this remote publishing actor.
	
	if ( Range.first != Range.second )
  {
		std::string ActorName ( ResolvedAddress.TheActor.AsString() ),
								EndpointID( ResolvedAddress.GlobalAddress.GetIP().to_string() );
	
		auto aSubscription = Range.first;
								
		while ( aSubscription != Range.second )
	  {
			std::string
			Topic( PublishingLink::SetTopic( ActorName, EndpointID, 
																		   aSubscription->second.name() ) );
			
			Subscription.subscribe( Topic );
			
			ActiveSubscriptions.emplace( Topic, ResolvedAddress.GlobalAddress, 
																	 ActorName );
			
			PendingRequests.erase( aSubscription++ );
		}
	}
}

// When messages starts arriving on the subscription socket, the handler will 
// be called. It will read the message off the socket and the first part of 
// the message will be the subscribed topic, and the second part the message 
// payload (content)
//
// It should be noted that it is not necessary to pass this message through 
// the session layer server or the presentation layer since it is known that 
// it is a serialised payload, and that the subscriber actor is the recipient. 
// Consequently, the payload can be directly sent to this actor (send to self).

void Theron::ZeroMQ::Subscriber::SubscriptionHandler( void )
{
	zmqpp::message         TheMessage;
	std::string            Topic;
	
	Subscription.receive( TheMessage );
	
	TheMessage >> Topic;
	
	auto TheSubscription = ActiveSubscriptions.find( Topic );
	
	if ( TheSubscription != ActiveSubscriptions.end() )
  {
		SerialMessage::Payload Payload;
		
		TheMessage >> Payload;
		
		// Sending the received payload back to ourself, impersonating the sender 
		// as the actor publishing this topic. This will invoke the handler for 
		// serialised messages, which will produce the binary message that will 
		// finally be delivered to the handler for the binary message type. 
		
		Send( Payload, 
					TheSubscription->second.Connection.GetActorAddress(), GetAddress() );
	}
}

// -----------------------------------------------------------------------------
// Constructor and destructor
// -----------------------------------------------------------------------------
//

Theron::ZeroMQ::Subscriber::Subscriber( const std::string & name )
: Actor( name ), StandardFallbackHandler( GetAddress().AsString() ), 
  DeserializingActor( GetAddress().AsString() ),
  Subscription( PublishingLink::GetContext(), zmqpp::socket_type::subscribe ),
  ActiveSubscriptions(), PendingRequests()
{ 
	PublishingLink::GetMonitor().add( Subscription, 
															 [this](void)->void{ SubscriptionHandler(); } );
	
	RegisterHandler( this, &Subscriber::ConnectSubscription );
}

// The destructor unsubscribes from all active subscriptions, and disconnect 
// from all active connections before removing the socket from the socket 
// monitor.

Theron::ZeroMQ::Subscriber::~Subscriber( void )
{
	// There can be many topics on the same connection, and so a set is used to
	// ensure that the recorded connection endpoints are all unique.
	
	std::set< std::string > ActiveConnections;
	
	// The subscribed topics are unsubscribed one by one. Note that there is 
	// no need to remove them from the map of active subscriptions since the 
	// everything will be deleted by the destructor of the active subscriptions 
	// map.
	
	for ( auto aSubcription = ActiveSubscriptions.begin(); 
			       aSubcription != ActiveSubscriptions.end(); ++aSubcription )
  {
		Subscription.unsubscribe( aSubcription->first );
		ActiveConnections.insert( aSubcription->second.Connection.GetEndpoint() );
	}
	
	// Then the recorded connections can be disconnected one by one.
	
	for ( const std::string & aConnection : ActiveConnections )
		Subscription.disconnect( aConnection );
	
	// Finally, the monitoring of the closing socket should be stopped.
	
	PublishingLink::GetMonitor().remove( Subscription );
}
