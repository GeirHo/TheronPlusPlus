/*=============================================================================
  Network End Point

  The communication architecture of Theron has an End Point class that
  deals with the external input and output activities.

  A network is served by three layers according to the Open Systems
  Interconnection model (OSI model) [1]: The Network Layer taking care of the
  link level protocol with other nodes in the system, the Session Layer taking
  care of the address mapping between the local actor addresses and their
  external addresses typically including information about the endpoint's
  network address (like actorX@nodeY), and the Presentation Layer ensuring
  that messages are correctly converted on sending. The three servers for
  these layers are implemented as actors, and they must all be up and running
  and correctly	connected before the network end point operational.

  Decoding inbound messages requires knowledge about the binary message format
  the network message will be converted into, and the available message formats 
  are defined by the receiving actor. Therefore, in order to receive messages 
  from remote actors, a local actor must be inheriting the base Networking
  Actor base class, and messages that are capable of being exchanged with 
  potentially remote actors should be derived from the Polymorphic Message 
  class that basically defines the protocol dependent payload conversion.

  References:
  [1] https://en.wikipedia.org/wiki/OSI_model

  Author and Copyright: Geir Horn, University of Oslo
  Contact: Geir.Horn@mn.uio.no
  License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
=============================================================================*/

#ifndef THERON_TRANSPARENT_COMMUNICATION
#define THERON_TRANSPARENT_COMMUNICATION

// Standard headers

#include <memory> 					// For shared pointers.
#include <utility>					// For the forward template
#include <map>							// For the managed actors
#include <set>							// For shut down management
#include <type_traits>			// For advanced meta-programming
#include <concepts>         // To constrain template types
#include <initializer_list> // For structured initialisation
#include <sstream>          // For readable error messages
#include <stdexcept>        // For error reporting
#include <thread>			      // To wait for all messages to be served
#include <chrono>			      // To wait if there are unhanded messages
#include <algorithm>	      // To accumulate unhanded messages
#include <source_location>  // For informative error messages

// The Theron++ actor framework

#include "Actor.hpp"			  
#include "Utility/StandardFallbackHandler.hpp"
#include "Communication/LinkMessage.hpp"

namespace Theron
{
// The base classes for the various layers are declared as known symbols

template< ValidLinkMessage ExternalMessage >
class NetworkLayer;

template < ValidLinkMessage ExternalMessage >
class SessionLayer;

template < class ProtocolPayload >
class PresentationLayer;

/*==============================================================================

 Network Layers

==============================================================================*/
//
// The actual implementations of the OSI layer server actors is network type
// dependent. Hence the constructors for the actors require technology specific
// parameters. At the same time the network endpoint class constructor must
// ensure that the network endpoint is operational and all server classes are
// running. A classical approach would be to define pure virtual functions on
// the network endpoint class to create and initialise each server type, and
// then leave to a network technology dependent sub-class to implement the
// actual server construction. However, this is not possible because the
// virtual function table is not initialised with the derived class' functions
// before the constructor of the base class, i.e. the network endpoint
// terminates. It is therefore not possible for the network endpoint 
// constructor to ensure that the network endpoint is operational when 
// terminating.
//
// Thus, it is required to have a class defining the network layer servers
// that can be inherited by a technology dependent network layer. This class
// is then passed as a template parameter to the network endpoint class, and
// will serve as a base class for the network endpoint class. This implies
// that the network endpoint class' constructor will execute after the
// constructor of the technology specific network layer, and the the endpoint
// specific initialisation can be done by the network base class.
//
// The network layer class is a virtual actor to allow it to register the
// handler for the shut down message whose functionality must be implemented
// by the endpoint.

class Network
: virtual public Actor,
  virtual public StandardFallbackHandler
{
  // ---------------------------------------------------------------------------
  // Network layers
  // ---------------------------------------------------------------------------
  //
  // The network layers are defined as an enumerated type defined according to
  // the OSI model

public:

  enum class Layer
  {
    Network,
    Session,
    Presentation
  };

  // ---------------------------------------------------------------------------
  // Storing layer servers
  // ---------------------------------------------------------------------------
  //
  // The actual storage of the communication layer servers is left to the
  // specific network type derived from this class. However, all servers
  // should be actors and it must be possible to obtain their addresses, and
  // each derived network type must define virtual address returning functions

protected:

  virtual Address NetworkLayerAddress     ( void ) const = 0;
  virtual Address SessionLayerAddress     ( void ) const = 0;
  virtual Address PresentationLayerAddress( void ) const = 0;

  // The problem with these is that one will need a pointer to the network
  // endpoint to obtain the addresses. A static function is therefore provided
  // to call indirectly these functions. This is defined in the code file for 
  // the generic Network Endpoint.
  //
  // The whole point of the static function is that it does not have the 'this'
  // pointer, and so in order to be able to call the address functions for 
  // the various layers, a static pointer to the Network class must be 
  // provided. This is initialised to 'this' in the constructor.

private:

  static const Network * TheNetwork; 

public:

  static Address GetAddress( Layer Role );
  
  // As this function overshadows the similar function from the actor, the
  // actor function is explicitly reused (differences in argument lists is
  // enough for the compiler to distinguish the two variants.)

  using Actor::GetAddress;

  // ---------------------------------------------------------------------------
  // Shut-down management
  // ---------------------------------------------------------------------------
  //
  // With independent actors distributed across multiple nodes, it is impossible
  // to say when the actor system has shut down and can close. Even though all
  // actors on this node has finished processing and there are no further
  // messages being processed locally, a message can be processed on a remote
  // node and this generates a response to an actor on this node making the
  // local actor system active again.
  //
  // It must therefore be a global shut-down protocol that must span all nodes
  // and try to ensure that no messages are in transit when the nodes closes.
  // For the local system this implies the following:
  //
  // 1. The Session Layer must inform all the Networking Actors that the
  //    node is shutting down. Any new Networking Actors registering will
  //    immediately receive the shut down message, and they will not be
  //    registered with remote endpoints.
  // 2. The Presentation Layer will be asked to stop any outbound message and
  //    just drop further messages quietly.
  // 3. The Networking Actors will each acknowledge the shut down message
  //    by sending a remove actor message to the Session Layer. This will
  //    inform the remote endpoints that these actors are gone.
  // 4. When the last Networking Actor asks for removal, the session layer
  //    will tell the Network Layer to shut down.
  // 5. The network layer should then disconnect from the peer endpoint overlay
  //    network, possibly informing the remote peers that this endpoint
  //    shuts down.
  //
  // This will ensure that the endpoint is disconnected. However the local actor
  // system can still be alive and keep sending messages, and the network actors
  // are also available to respond to messages. The local actor system can be
  // disconnected when there are no more messages being processed by any actor,
  // see the global Actor::WaitForTermination().
  //
  // It is application dependent behaviour how to handle the shut down message
  // from a remote end point. Some may take such a message as a shut down signal
  // to this local actor system, and some may just ignore it and accept that
  // the remote system is gone. It is therefore recommended that the Network
  // Layer server sends the external shut down message to the Network Endpoint
  // actor and the function handling this message can be overloaded to do more
  // specific handling.

public:

  class ShutDown
  {
  public:

    ShutDown( void ) = default;
    ShutDown( const ShutDown & Other ) = default;
  };

protected:

  virtual void Stop( const ShutDown & StopMessage, const Address Sender )
  {
    Send( StopMessage, SessionLayerAddress() );
  }

  // The remote endpoint shut down is more complicated because that means that
  // all connections against that end-point are invalidated. There is a 
  // dedicated message that is used by the Network Layer to inform the 
  // Session Layer that all references to the remote endpoint should be removed.
  // At the same time all connections held against the endpoint by the 
  // Network Layer should automatically be closed. 
  //
  // The issue is that the global address will be protocol specific, and the 
  // generic closing endpoint message cannot contain the address object 
  // directly. It will instead exploit the fact that all global addresses 
  // must have the Global Address object as a base class, and it is therefore 
  // possible to obtain the endpoint identifier string from any leagal global
  // address. 
                             
public:
  
  class ClosingEndpoint
  {
  public:
    
    const std::string Identifier;
    
    template< class TheGlobalAddressType >
    requires std::derived_from< TheGlobalAddressType, GlobalAddress >
    ClosingEndpoint( const TheGlobalAddressType & TheClosingEndpoint )
    : Identifier( TheClosingEndpoint.Endpoint() )
    {}

    ClosingEndpoint ( void ) = delete;
    ClosingEndpoint ( const ClosingEndpoint & Other ) = default;
    ~ClosingEndpoint( void ) = default;
  };

  // ---------------------------------------------------------------------------
  // Constructor and destructor
  // ---------------------------------------------------------------------------
  //
  // The default constructor is made protected to ensure that only the network
  // endpoint can call it, and hence any derived class cannot be instantiated
  // even though it defines the proper virtual functions. The copy constructor
  // is explicitly deleted for the same reasons.

  Network( const std::string & Name )
  : Actor( Name ), StandardFallbackHandler( Actor::GetAddress().AsString() )
  {
    RegisterHandler( this, &Network::Stop );
    TheNetwork = this;
  }

  Network( void ) = delete;
  Network( const Network & Other ) = delete;

  // The destructor is public and virtual to allow the proper destruction of
  // the derived classes. It also resets all the shared pointers for the layer
  // servers, effectively killing these.

public:

  virtual ~Network( void )
  { }
};

/*==============================================================================

 Network endpoint

==============================================================================*/
//
// The network endpoint class is a template taking a network type class as
// parameter. The details about this will be explained below under
// initialisation, and it is necessary to ensure that the network endpoint
// is consistent when its constructor terminates, i.e. that all the three
// layer actors are properly initialised. Hence, the given network type must
// be derived from the Network base class for the network endpoint class to
// compile.

template< class NetworkType >
requires std::derived_from< NetworkType, Network >
class NetworkEndpoint
: virtual public Actor,
  virtual public StandardFallbackHandler,
  public NetworkType
{
public:

  // ---------------------------------------------------------------------------
  // Network layers
  // ---------------------------------------------------------------------------
  //
  // Since it is known that the network type is a network, it does define the
  // enumeration of the network layers.

  using typename NetworkType::Layer;

  // The address functions are also reused

  using NetworkType::NetworkLayerAddress;
  using NetworkType::SessionLayerAddress;
  using NetworkType::PresentationLayerAddress;

  // ---------------------------------------------------------------------------
  // Constructor
  // ---------------------------------------------------------------------------
  //
  // The constructor requires the name of the network endpoint and possibly
  // other parameters to be forwarded to the specific constructor of the
  // transport technology network class responsible for creating and
  // initializing the network layer servers. Their availability is tested by
  // checking their addresses, and a logic error is thrown if one of the
  // addresses are not defined.

public:

 template< typename ... NetworkParameterTypes >
 NetworkEndpoint( const std::string & EndpointName,
                  NetworkParameterTypes && ... NetworkParameterValues )
  : Actor( EndpointName ), 
    StandardFallbackHandler( Actor::GetAddress().AsString() ),
    NetworkType( Actor::GetAddress().AsString(),
           std::forward< NetworkParameterTypes >(NetworkParameterValues)... )
  {
    // The validity of the network layer addresses is tested to ensure that
    // the network type class has started all communication layer servers.

    std::string LayerServerError;

    if ( NetworkLayerAddress() == Address::Null() )
      LayerServerError = "network";
    else if ( SessionLayerAddress() == Address::Null() )
      LayerServerError = "session";
    else if ( PresentationLayerAddress() == Address::Null() )
      LayerServerError = "presentation";

    if ( !LayerServerError.empty() )
    {
      std::ostringstream ErrorMessage;
      std::source_location Location = std::source_location::current();

      ErrorMessage << Location.file_name() << " at line " 
                   << Location.line() << "in" << Location.function_name() << ": "
                   << "Network endpoint - No server for the "
                   << "communication layer " << LayerServerError
                   << " has been defined. Improper initialisation!";

      throw std::logic_error( ErrorMessage.str() );
    }
  }

  // The destructor is also not doing anything particular since the managed
  // actors for the network layers will be destroyed by the unique pointer
  // destructor

  virtual ~NetworkEndpoint( void ) = default;
};

}  			// End name space Theron
#endif  // THERON_TRANSPARENT_COMMUNICATION
