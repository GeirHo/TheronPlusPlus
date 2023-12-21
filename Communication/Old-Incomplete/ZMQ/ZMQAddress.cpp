/*==============================================================================
Zero Message Queue (ZMQ) Address

This file implements the logic of the TCP address. When the Network layer
starts up it needs to bind the local ports to the sockets and then broadcast
its addresses to its fellow peers, an new peers as they become available.
Finding the own IP address is a non-trivial task, and the following
implementation is a mix of information from several sources on how to use
the getifaddrs function and interpret the results. The manual page [1] gives
the basic information but it does not document how to convert the obtained
results to a useful form. The best example was provided by Twelve42 in a
Stack Exchange response [2] that also shows how to use the inet_ntop [3]
function to convert the address to a useful string.

References:
[1] https://linux.die.net/man/3/getifaddrs
[2] https://stackoverflow.com/questions/212528/get-the-ip-address-of-the-machine
[3] https://linux.die.net/man/3/inet_ntop

Author and Copyright: Geir Horn, University of Oslo, 2017-2019
License: LGPL 3.0
==============================================================================*/

#include <string>                            // Standard strings
#include <stdexcept>										     // Standard exceptions
#include <sstream>											     // For error reporting

#include <sys/types.h>									     // Types used for IP address lookup
#include <ifaddrs.h>                         // The getifaddrs function
#include <arpa/inet.h>                       // Converting IP info to string

#include <boost/system/error_code.hpp>       // Boost error reporting
#include <boost/algorithm/string.hpp> 	     // To convert to lower-case

#include <zmqpp.hpp>				 	               // ZeroMQ bindings for C++

#include "Communication/ZMQ/ZMQAddress.hpp"

/*=============================================================================

 Get the IP address of the computer

=============================================================================*/
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

 TCP Address

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

// -----------------------------------------------------------------------------
// Constructing from address string
// -----------------------------------------------------------------------------
//
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
: IP(), PortNumber(0)
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

/*=============================================================================

 Network Address

=============================================================================*/
//
// Converting the endpoint address from a string is slightly more difficult
// because the string could be the an IP address, possibly with port number,
// or it could be an inter process communication (IPC) channel of the form
// <actor name>@<port number>. Note that the @<port number> may be missing if
// the actor name is sufficient to identify the IPC channel. The parsing
// is done by checking the protocol prefix of the endpoint string given
// and then parse it accordingly. In C++20 there will be a function to test
// if a string starts with a prefix. There seems to be limited compiler
// support for this new feature and the traditional find function is
// therefore used.

Theron::ZeroMQ::NetworkAddress::NetworkAddress(
	const std::string & TheEndpoint, const std::string & TheActor)
{
  if ( TheEndpoint.find( "tcp://") != std::string::npos )
	{
		EndpointID = TCPAddress( TheEndpoint );
		ActorID    = TheActor;
	}
	else if ( TheEndpoint.find( "ipc://") != std::string::npos )
  {
		std::string IPCChannel( TheEndpoint.substr( 6 ) );

		// The channel string is then split into the actor name and the port if
		// it contains the @ sign. Otherwise, it is taken to be just the name of
		// the actor and no port will be specified. In both cases will the IP
		// address of the endpoint be left void.

		auto AtSignPosition = IPCChannel.find('@');

		if ( AtSignPosition == std::string::npos )
		{
			EndpointID = TCPAddress();
			ActorID    = IPCChannel;
		}
		else
		{
			EndpointID = TCPAddress( IPAddress(),
										 std::stoul( IPCChannel.substr( AtSignPosition + 1 ) ) );
			ActorID = IPCChannel.substr( 0, AtSignPosition - 1 );
		}
	}
	else
  {
		std::ostringstream ErrorMessage;

		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
		             << "Network address: The format of the endpoint string "
								 << "\"" << TheEndpoint << "\" given does not specify a "
								 << "supported protocol (tcp:// or irc://)";

	  throw std::logic_error( ErrorMessage.str() );
	}
}


// -----------------------------------------------------------------------------
// Transmitting the network address
// -----------------------------------------------------------------------------
//
// The network address is inserted into the ZMQ message as two frames by
// overloading the stream operator.

zmqpp::message & operator << (zmqpp::message & MessageToSend,
											        const Theron::ZeroMQ::NetworkAddress & TheAddress)
{
	MessageToSend << TheAddress.GetEndpoint() << TheAddress.GetActorName();

	return MessageToSend;
}

// In the same way it can be extracted from a message

zmqpp::message & operator >> ( zmqpp::message & MessageReceived,
			 										     Theron::ZeroMQ::NetworkAddress & TheAddress )
{
	std::string TheEndpoint, TheActor;

	MessageReceived >> TheEndpoint >> TheActor;

	TheAddress = Theron::ZeroMQ::NetworkAddress( TheEndpoint, TheActor );

	return MessageReceived;
}

