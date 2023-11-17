/*==============================================================================
Zero Message Queue (ZMQ) Address

The ZMQ library requires real IP addresses to be used, and therefore the
external addressing scheme is based on the IP addresses provided with the Boost
ASIO implementation. An external actor address is therefore a combination of
an endpoint's IP address and a standard string being the actor name.

In addition this file defines the TCP ports used by the Theron++ ZMQ layers.
Assigning the TCP ports to be used by the actor system on the endpoints is
a matter of choice. Not all ports can be freely used, and many ports are
already registered for various server applications [1]. Given that it is
not possible to know what other applications that are running on the
computers hosting the actor system, one should only use ports from the
dynamic or private range (49152–65535) that cannot be registered
with IANA [2]. The mobile SSH like shell Mosh [3] uses the port range
60000–61000. Hence, this range should be avoided. Furthermore, the port
range should be easy to remember, and normally ranges are continuous.

References:
[1] https://en.wikipedia.org/wiki/List_of_TCP_and_UDP_port_numbers
[2] https://www.iana.org/
[3] https://mosh.org/

Author and Copyright: Geir Horn, University of Oslo, 2017-2019
License: LGPL 3.0
==============================================================================*/

#ifndef THERON_ZMQ_EXTERNAL_ADDRESS
#define THERON_ZMQ_EXTERNAL_ADDRESS

#include <boost/asio/ip/address.hpp> // IP address class

#include "Actor.hpp"                 // Theron++ actor Addresses

namespace Theron::ZeroMQ
{
/*==============================================================================

 TCP ports and addresses

==============================================================================*/
//
// Given the above restrictions and considerations for choosing the IP ports,
// the following will be used for the Theron++ communication.

constexpr auto DiscoveryPort = 50505;
constexpr auto InboundPort 	 = 50506;
//constexpr auto CommandPort   = 50507;
//constexpr auto DataPort      = 50508; // Publish data messages

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

/*==============================================================================

 TCP addresses

==============================================================================*/
//
//
// With the IP address type defined, it is easy to define a TCP endpoint that
// consists of an IP address and a port number.

class TCPAddress
{
private:

  IPAddress          IP;
	unsigned short int PortNumber;

public:

  // ---------------------------------------------------------------------------
  // Constructors
  // ---------------------------------------------------------------------------
	//
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
	// the code file. The address must in this case be a legal TCP protocol
	// string "tcp://<ip-address>:<port>"

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

  // ---------------------------------------------------------------------------
  // Interface functions
  // ---------------------------------------------------------------------------
	//
	// Then it is possible to ensure that the string representation of the TCP
	// address is correct.

	inline std::string AsString( void ) const
	{
		return IP.to_string() + ":" + std::to_string( PortNumber );
	}

	// This converter function is also used by the conversion operator

	inline operator std::string () const
	{ return AsString(); }

	// Then there are interface functions to get the IP and port number

	inline IPAddress GetIP( void ) const
	{ return IP; }

	inline unsigned short int GetPort( void ) const
	{ return PortNumber; }

  // ---------------------------------------------------------------------------
  // Comparators
  // ---------------------------------------------------------------------------
	//
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

	inline bool operator > ( const TCPAddress & Other ) const
	{
		if ( IP > Other.IP )
			return true;
		else
			if ( (IP == Other.IP) && (PortNumber > Other.PortNumber) )
				return true;
			else
				return false;
	}

	inline bool operator >= ( const TCPAddress & Other ) const
	{
		if ( IP >= Other.IP )
			return true;
		else
			if ( (IP == Other.IP) && (PortNumber >= Other.PortNumber) )
				return true;
			else
				return false;
	}

	inline bool operator <= ( const TCPAddress & Other ) const
	{
		if ( IP <= Other.IP )
			return true;
		else
			if ( (IP == Other.IP) && (PortNumber <= Other.PortNumber) )
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
//
// However, since ZMQ supports Inter Process Communication (IPC), it can be
// that the Network address refers to a IPC channel on the current endpoint.
// It should be noted that using IPC is not portable. It should be supported
// on all POSIX compliant systems, but the safe option for portable systems
// is to use a loop-back from the TCP layer using ports on the local host.
//
// The main difference in TCP communication and IPC communication is the use of
// the TCP address of the endpoint. In the case of TCP this is void

class NetworkAddress
{
private:

	  TCPAddress  EndpointID;
		std::string ActorID;

public:

  // ---------------------------------------------------------------------------
  // Constructors
  // ---------------------------------------------------------------------------
  //
	// The constructor is used to initialise the IDs. Note that the actor ID can
	// be given as an address, and a separate constructor is provided for this
	// situation.

	inline NetworkAddress( const TCPAddress & TheEndpoint,
												 const Address & TheActor )
	: EndpointID( TheEndpoint ), ActorID( TheActor.AsString() )
	{ }

	inline NetworkAddress( const TCPAddress & TheEndpoint,
												 const std::string & TheActor )
	: EndpointID( TheEndpoint ), ActorID( TheActor )
	{ }

	// When the endpoint is a string it becomes more complicated as it could then
	// represent an IP endpoint of the form <IP address>:<port number> or it may
	// be a representation of an Inter Process Communication tag that is encoded
	// as <actor name>@<port name> and it will be encoded as a TCP address with
	// an empty IP. A logic error is thrown if the given endpoint string cannot
	// be parsed into any legal format. This is a relatively complex parsing so
	// it is left for the source file.

	NetworkAddress( const std::string & TheEndpoint,
									const std::string & TheActor );

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
  //
  // First some simple functions basically returning variants of the stored
  // fields

  inline IPAddress GetIP( void ) const
	{ return EndpointID.GetIP(); }

	inline std::string GetIPString( void ) const
	{ return GetIP().to_string(); }

	inline TCPAddress GetEndpointLocation( void ) const
	{ return EndpointID; }

	inline std::string GetActorName( void ) const
	{ return ActorID; }

	inline Address GetActorAddress( void ) const
	{ return Address( ActorID ); }

	// There are functions to check if the address is for a TCP or an IPC
	// connection, or if it is a legal address

	inline bool IsTCP( void ) const
	{ return EndpointID.GetIP() != IPAddress(); }

	inline bool IsIPC( void ) const
	{ return EndpointID.GetIP() == IPAddress(); }

	inline bool IsLegal( void ) const
	{
		if ( ( EndpointID.GetIP() == IPAddress() ) && ( ActorID.empty() ) )
			return false;
		else
			return true;
	}

	inline operator bool () const
	{ return IsLegal(); }

	// The protocol is a string that is a prefix for the actual endpoint address

	inline std::string Protocol( void ) const
	{
		if ( IsIPC() )
			return "ipc://";
		else
			return "tcp://";
	}

	// There will also be a need to return unique endpoint IDs (with port numbers)
	// or the simple IP address (with no port number if this is an TCP address)
	// as strings. The address is the same in the case of IPC for which the
	// channel name will be the name of the actor followed by underscore and the
	// port number. The endpoint will equal only the actor ID if the port number
	// is zero.

	inline std::string GetEndpoint( void ) const
	{
		if ( IsIPC() )
		{
			if ( EndpointID.GetPort() == 0)
				return ActorID;
			else
			return ActorID + '@' + std::to_string( EndpointID.GetPort() );
		}
		else
			return EndpointID.AsString();
	}

	inline std::string GetEndpointIP( void ) const
	{
		if ( IsIPC() )
		{
			if ( EndpointID.GetPort() == 0)
				return ActorID;
			else
			return ActorID + '@' + std::to_string( EndpointID.GetPort() );
		}
		else
			return GetIPString();
	}

	// There are also functions to convert the network address to an endpoint
	// string (with the protocol) depending on the type of address it contains.
	// This full network address is what constitutes the ZMQ endpoint string,
	// and so the address can readily be converted to the endpoint string.

	inline std::string AsString( void ) const
	{ return Protocol() + GetEndpoint(); }

	inline operator zmqpp::endpoint_t () const
	{ return AsString(); }

	// There is also a stream operator to print the address on a standard
	// output stream

	friend std::ostream & operator << ( std::ostream & OutputStream,
																      const NetworkAddress & ToPrint )
	{
		OutputStream << ToPrint.AsString();
		return OutputStream;
	}

  // ---------------------------------------------------------------------------
  // Comparators
  // ---------------------------------------------------------------------------
	//
	// In order to ensure proper routing of messages, it it necessary to be able
	// to compare addresses. Both the endpoint ID and the actor ID must match for
	// two addresses to be considered equal.

	inline bool operator == ( const NetworkAddress & Other ) const
	{
		return (EndpointID == Other.EndpointID) && (ActorID == Other.ActorID);
	}

	inline bool operator > ( const NetworkAddress & Other ) const
	{
		if ( EndpointID > Other.EndpointID )
			return true;
		else if ( (EndpointID == Other.EndpointID) && (ActorID > Other.ActorID) )
			return true;
		else
			return false;
	}

	inline bool operator < ( const NetworkAddress & Other ) const
	{
		if ( EndpointID < Other.EndpointID )
			return true;
		else if ( (EndpointID == Other.EndpointID) && (ActorID < Other.ActorID) )
			return true;
		else
			return false;
	}

	inline bool operator >= ( const NetworkAddress & Other ) const
	{
		if ( EndpointID >= Other.EndpointID )
			return true;
		else if ( (EndpointID == Other.EndpointID) && (ActorID >= Other.ActorID) )
			return true;
		else
			return false;
	}

	inline bool operator <= ( const NetworkAddress & Other ) const
	{
		if ( EndpointID <= Other.EndpointID )
			return true;
		else if ( (EndpointID == Other.EndpointID) && (ActorID <= Other.ActorID) )
			return true;
		else
			return false;
	}

};
}      // End name space Theron::ZeroMQ

// -----------------------------------------------------------------------------
// Transmitting the network address
// -----------------------------------------------------------------------------
//
// There are two stream operators to insert an address into a ZMQ message or
// extract the address from an ZMQ message frame.

zmqpp::message & operator <<
( zmqpp::message & MessageToSend,
	const Theron::ZeroMQ::NetworkAddress & TheAddress);

zmqpp::message & operator >>
( zmqpp::message & MessageReceived,
	Theron::ZeroMQ::NetworkAddress & TheAddress );

// -----------------------------------------------------------------------------
// Hash function
// -----------------------------------------------------------------------------
//
// There is a hash function to allow the use of the network address in
// unordered containers. This must be defined as a specialisation in the
// standard name space for it to be used as the default hash function for
// containers with a network address as key. It only converts the address to
// a string and then use the standard hash function for strings on the result.

namespace std {

  template<>
  class hash< Theron::ZeroMQ::NetworkAddress >
  {
  public:

    size_t operator() (const Theron::ZeroMQ::NetworkAddress & TheAddress ) const
    {
      return std::hash< string >()( TheAddress.AsString() );
    }
  };

}	// End name space std

#endif // THERON_ZMQ_EXTERNAL_ADDRESS
