/*=============================================================================
  XMPP Endpoint

  The network implements the XMPP protocol [1] with the use of the Swiften
  library [2]. This library has to be built from scratch for most operating
  systems (it is packaged by the developers for Debian based OSes).
  Fedora 31: The build process is straight forward and also installs the header
  files in /usr/local/include.

  The Network interface allocates the three servers needed for the transparent
  communication: The Network Layer, the Session Layer, and the Presentation
  Layer and defines the functions to get the addresses of these actors.

  Author and copyright: Geir Horn, University of Oslo, 2015 - 2020
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef THERON_XMPP
#define THERON_XMPP

// Theron++ Actor Framework headers

#include "Actor.hpp"
#include "Utility/StandardFallbackHandler.hpp"

#include "Communication/NetworkEndpoint.hpp"
#include "Communication/XMPP/XMPPMessages.hpp"
#include "Communication/XMPP/XMPPNetworkLayer.hpp"
#include "Communication/XMPP/XMPPSessionLayer.hpp"
#include "Communication/XMPP/XMPPPresentationLayer.hpp"

namespace Theron::XMPP
{
/*==============================================================================

 XMPP Network

==============================================================================*/
//
// The network class is responsible for creating the different layer servers
// using the framework of the generic network class.

class Network
: virtual public Actor,
  virtual public StandardFallbackHandler,
  public Theron::Network
{
	// ---------------------------------------------------------------------------
  // Communication servers
  // ---------------------------------------------------------------------------
	//
	// This XMPP Network class is final class and it should not be further
	// inherited. The network layer actors can therefore be direct data member
	// of this class.

private:

	XMPP::NetworkLayer      NetworkServer;
	XMPP::SessionLayer      SessionServer;
	XMPP::PresentationLayer PresentationServer;

  // ---------------------------------------------------------------------------
  // Address access
  // ---------------------------------------------------------------------------
	//
	// The addresses of these layer servers are returned by virtual functions
	// that are so simple that they can be defined in-line

protected:

	virtual Address NetworkLayerAddress( void ) const final
	{ return NetworkServer.GetAddress(); }

	virtual Address SessionLayerAddress( void ) const final
	{ return SessionServer.GetAddress(); }

	virtual Address PresentationLayerAddress( void ) const final
	{ return PresentationServer.GetAddress(); }

	// In order to provide access to the server addresses without having a pointer
	// to this class, a static pointer is defined and used by a static function
	// to call the right address function.

private:

	static Network * XMPPNetwork;

		// Then there is a public function to obtain the addresses based on the
	// layer types defined in the standard network class.

public:

	static Address GetAddress( Theron::Network::Layer Role );

  // As this function overshadows the similar function from the actor, the
  // actor function is explicitly reused (differences in argument lists is
  // enough for the compiler to distinguish the two variants.)

  using Actor::GetAddress;

  // ---------------------------------------------------------------------------
  // Shut down management
  // ---------------------------------------------------------------------------
	//
	// There is no special shut down management for XMPP networks. The standard
	// mechanisms provided by the network is sufficient.

	  // ---------------------------------------------------------------------------
  // Constructor and destructor
  // ---------------------------------------------------------------------------
	//
	// The constructor must have an endpoint name, and it should be noted that
	// This is the external name to be used towards the remote XMPP endpoints
	// giving actor addresses like <endpoint name>@<domain>/<actor name>. The
	// endpoint name is also the name of the Network Layer server - not the
	// endpoint actor. To avoid a name clash for the network server actors, an
	// optional endpoint prefix can be given. This defaults to "XMPP:" and so the
	// name of a server actor will be "XMPP:<servername>" and the endpoint
	// Network actor, the Session Layer server, and the Presentation layer
	// server will all receive this name.
	//
	// The XMPP message broker is supposed to run at the same domain as the one
	// used in the address for the XMPP interface, which may be different from
	// where the XMPP endpoint actually runs. The logic is that in order
	// to access this endpoint, one needs to send a message through the broker
	// and so one does not need to know the physical address of all the XMPP
	// clients connected to this server.
	//
	// The initial remote endpoint is the address of the initial peer that will
	// send its rooster to this endpoint in order to pass on the knowledge of
	// all the already registered actors in the system. The very first peer that
	// connects should have this as empty since there are no other XMPP endpoint
	// to consult to obtain the initial roster.
	//
	// Finally, the names for the Session Layer server and the Presentation Layer
	// server can be given. They are only used to create the named actors,
	// and default names are used if they are not given. The final argument to
	// the constructor is the optional endpoint prefix to be added to these
	// server names and to the network endpoint actor.

protected:

	Network( const std::string & EndpointName,
					 const std::string & Domain,
					 const std::string & ServerPassword,
					 const JabberID    & InitialRemoteEndpoint = JabberID(),
					 const std::string & SessionServerName = "SessionLayer",
					 const std::string & PresentationServerName = "PresentationLayer",
					 const std::string & Prefix = "XMPP:" );

	// The default constructor and the copy constructor are deleted

	Network( void ) = delete;
	Network( const Network & Other ) = delete;

	// The destructor is virtual to ensure proper closing of base classes

	virtual ~Network();
};

/*==============================================================================

 XMPP Endpoint

==============================================================================*/
//
// Setting up the endpoint for XMPP implies simply to reuse the standard network
// endpoint for the above network class creating the right servers.

using NetworkEndpoint = Theron::NetworkEndpoint< Theron::XMPP::Network >;

}      // Name space Theron XMPP
#endif // THERON_XMPP
