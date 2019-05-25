/*=============================================================================
  Network End Point

  The original communication architecture of Theron has an End Point class that
  deals with the external input and output activities. In addition there is a
  Framework class dealing with all the actors and the message passing inside
  the network endpoint. In order to allow node external communication, the
  Framework has to be created (bound) to the the End Point object.

  With the aim of creating transparent communication these two classes should
  fundamentally merge into the same mechanism hosting actors and taking care
  of their communication. This is the purpose of this class.

  It encompasses the Network Layer Server, the Session Layer Server and the
  Presentation Layer server to ensure that they are confirming to the
  communication protocol of the actor system. In addition, there can be only
  one network endpoint actor at each node.

  Author: Geir Horn, University of Oslo, 2015-2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef THERON_TRANSPARENT_COMMUNICATION
#define THERON_TRANSPARENT_COMMUNICATION

#include <memory> 					// For shared pointers.
#include <utility>					// For the forward template
#include <map>							// For the managed actors
#include <set>							// For shut down management
#include <type_traits>			// For advanced meta-programming
#include <initializer_list> // For structured initialisation
#include <sstream>          // For readable error messages
#include <stdexcept>        // For error reporting
#include <thread>			      // To wait for all messages to be served
#include <chrono>			      // To wait if there are unhanded messages
#include <algorithm>	      // To accumulate unhanded messages

#include "Actor.hpp"			 // The Theron++ actor framework
#include "Utility/StandardFallbackHandler.hpp"

namespace Theron
{

// The base classes for the various layers are declared as known symbols

template< class ExternalMessage >
class NetworkLayer;

template < class ExternalMessage >
class SessionLayer;

class PresentationLayer;

/*==============================================================================

 Network Layers

==============================================================================*/
//
// A network is served by three layers: The network layer taking care of the
// link level protocol with other nodes in the system, the session layer taking
// care of the address mapping between the local actor addresses and their
// external addresses typically including information about the endpoint's
// network address (like actorX@nodeY), and the presentation layer ensuring
// that messages are correctly serialised on sending and de-serialised on
// arrival (see the serial message header). The three servers for these layers
// are implemented as actors, and they must all be up and running and correctly
// connected before the network end point operational.
//
// This creates a problem in that the actual implementations of the server
// actors can be network type dependent. Hence the constructors for the actors
// can require technology specific parameters. At the same time the network
// endpoint class constructor must ensure that the network endpoint is
// operational and all server classes are running. A classical approach would
// be to define pure virtual functions on the network endpoint class to create
// and initialise each server type, and then leave to a network technology
// dependent sub-class to implement the actual server construction. However,
// this is not possible because the virtual function table is not initialised
// with the derived class' functions before the constructor of the base class,
// i.e. the network endpoint terminates. It is therefore not possible for the
// network endpoint constructor to ensure that the network endpoint is
// operational when terminating. The danger is that the base class fails to
// start a server.
//
// It is therefore required to have a class defining the network layer servers
// that can be inherited by a technology dependent network layer. This class
// is then passed as a template parameter to the network endpoint class, and
// will serve as a base class for the network endpoint class. This implies
// that the network endpoint class' constructor will execute after the
// constructor of the technology specific network layer, and the virtual
// functions will be correctly initialised depending on the used network layers
// protocol stack.
//
// The network layer class is a virtual actor to allow it to register the
// handler for the shut down message whose functionality must be implemented
// by the endpoint.

class Network
: virtual public Actor,
  virtual public StandardFallbackHandler
{
public:

  // ---------------------------------------------------------------------------
  // Network layers
  // ---------------------------------------------------------------------------
	//
	// The network layers are defined as an enumerated type defined according to
	// the OSI model (https://en.wikipedia.org/wiki/OSI_model)

  enum class Layer
  {
    Network,
    Session,
    Presentation
  };

private:

  // ---------------------------------------------------------------------------
  // Network layer servers
  // ---------------------------------------------------------------------------
	//
  // Independent on how the servers are implemented, we know that they must be
  // actors, and hence we can keep a pointer to them as actors. Note that the
  // pointer must be shared to allow the access to these pointers from derived
  // classes (unique_ptr cannot be used as they cannot be copied).
  //
  // Since all operations on these actors will be similar, there is no need
  // to implement dedicated functions for each of the three actors, e.g. to
  // obtain the Theron Address of the actor. They can be accessed by the
  // function they have, and the pointers are therefore kept in a map.
	//
	// Since there can be only one network end point, this map is a static member
	// so that the addresses for the actors can be obtained without having a
	// pointer to the network end point class.

  static std::map< Layer, std::shared_ptr< Actor > > CommunicationActor;

	// However, the network endpoint should be allowed to access these servers
	// and it should therefore be granted access independent of which network
	// type it has. Note that the protection of the map is essential because it
	// prevents a derived technology specific network type to directly set the
	// servers, and forces the use of the virtual functions.

	template< class NetworkType, class Enable >
	friend class NetworkEndpoint;

  // ---------------------------------------------------------------------------
  // Getting Network Endpoint Layers
  // ---------------------------------------------------------------------------
  // The core functionality is to encapsulate the necessary communication
  // actors: The Network Layer server dealing with the physical link exchange
  // and sockets; the Session Layer taking care of the the mapping of external
  // actor addresses and actor addresses on this framework; and the Presentation
  // layer server allowing actors to send messages transparently to actors on
  // remote endpoints.
	//
  // In some cases it can be necessary to directly address these actors,
  // for extended initialisation messages to provided handlers. The following
  // access function returns the actor address provided that the
  // corresponding actor has been created.

public:

  inline static Address GetAddress( Layer Role )
  {
    std::shared_ptr< Actor > TheServer = CommunicationActor[ Role ];

    if ( TheServer )
      return TheServer->GetAddress();
    else
      return Address::Null();
  }

  // In some cases it could be desired to get the address of the network
  // end point itself. This would correspond to calling the get address on
  // the network endpoint object. However, since there is only one network
  // endpoint the address could be a global variable, or better there could
  // be a static function returning the address similar to the previous
  // function. This requires that there is a static variable holding the pointer
  // to this endpoint, which is initialised by the constructor when the
  // network endpoint is created.

private:

	static Actor * ThisNetworkEndpoint;

public:

	inline static Address GetAddress( void )
	{
		if ( ThisNetworkEndpoint != nullptr )
			return ThisNetworkEndpoint->Actor::GetAddress();
		else
			return Address::Null();
	}

  // ---------------------------------------------------------------------------
  // Creating the layers
  // ---------------------------------------------------------------------------
	//
	// Another difficulty is to ensure that a given class is really derived from
	// the expected protocol layer base class. This is ensured by a special
	// template to create a server. This template will make a test to see if
	// the given server type is derived from the correct base class. This test
	// is based on defining the network layer as a private parameter of the base
	// class. In order to allow this to be tested it is required that a derived
	// network technology specific class uses a template function to create the
	// servers. The creator method takes the function of the actor to be created
  // as first argument, and then the other arguments are forwarded to the
  // actor's constructor.
  //
  // The arguments are passed using the forwarding mechanism to be able to
  // handle all kind of different arguments, see the excellent answer at
  // http://stackoverflow.com/questions/3582001/advantages-of-using-forward
  // for details. Note that when unrolling the argument list the forward
  // template is iterated over all arguments (types and values) This iteration
  // taken from http://eli.thegreenplace.net/2014/variadic-templates-in-c/

protected:

  template< Layer Role, class ServerClass, typename ... ArgumentTypes >
  inline void CreateServer( ArgumentTypes && ... ConstructorArguments )
  {
		// Testing if it is derived from the right base class

		if constexpr ( Role == Layer::Network )
			static_assert(
				std::is_base_of< NetworkLayer< typename ServerClass::MessageType >,
												 ServerClass >::value,
				"Network: Server must be derived from the Network Layer base class" );
		else if constexpr ( Role == Layer::Session )
			static_assert(
				std::is_base_of< SessionLayer< typename ServerClass::MessageType >,
												 ServerClass >::value,
				"Network: Server must be derived from Session Layer base class" );
		else if constexpr ( Role == Layer::Presentation )
			static_assert( std::is_base_of< PresentationLayer, ServerClass >::value,
		  "Network: Server must be derived from the Presentation Layer base class");

		// The server is derived from the correct base class, which implicitly
		// makes it an actor, and it can be stored as one of the network layers.
		// The emplace function ensures that the server is stored only once, and
		// trying to register a second server for the same role should result in
		// an exception.

		if (! CommunicationActor.emplace( Role,
						new ServerClass(
						 std::forward< ArgumentTypes >(ConstructorArguments)... ) ).second )
		{
			const std::map< Layer, std::string > LayerStrings = {
				{ Layer::Network, "network" },
				{ Layer::Session, "session" },
				{ Layer::Presentation, "presentation"}
			};

			std::ostringstream ErrorMessage;

			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "A network layer server for the "
									 << LayerStrings.at( Role )
									 << " layer is already defined."
									 << " Duplicate server definitions are not allowed";

			throw std::logic_error( ErrorMessage.str() );
		}
  }

  // The virtual functions to create each type of server must use this create
  // server function for the server to be properly stored in the map.

  virtual void CreateNetworkLayer( void ) = 0;
	virtual void CreateSessionLayer( void ) = 0;
	virtual void CreatePresentationLayer( void ) = 0;

	// Finally, there could be a need to link the various servers in some way
	// depending on the communication technology, and there is a function to
	// bind the servers. Note that this function will be called by the network
	// endpoint class constructor after the creation functions, and after checking
	// that all layers have been properly defined. By default it does nothing.

	virtual void BindServers( void )
	{ }

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
	// 1. The session layer must inform all the de-serializing actors that the
	//    node is shutting down. Any new de-serializing actors registering will
  //    immediately receive the shut down message, and they will not be
  //    registered with remote endpoints.
  // 2. The Presentation layer will be asked to stop any outbound message and
  //    just drop further messages quietly.
	// 3. The de-serializing actors will each acknowledge the shut down message
	//    by sending a remove actor message to the session layer. This will
	//    inform the remote endpoints that these actors are gone.
	// 4. When the last de-serializing actor asks for removal, the session layer
	//    will tell the network layer to shut down.
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
	// Layer server sends the shut down message to the Network Endpoint actor
	// and the function handling this message can be overloaded to do more
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
		Send( StopMessage, GetAddress( Layer::Session ) );
	}

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
		ThisNetworkEndpoint = this;
		RegisterHandler( this, &Network::Stop );
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

template< class NetworkType,
	typename = std::enable_if_t< std::is_base_of<Network, NetworkType>::value > >
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

private:

	// The map of the various actors is defined to be a part of this class too
	// so it can be accessed by user level interface functions.

	using NetworkType::CommunicationActor;

  // ---------------------------------------------------------------------------
  // Network layer server pointer
  // ---------------------------------------------------------------------------
	//
	// In some situations one would need a pointer to one of the layer servers,
	// and this access can only be given by the network end point as it ensures
	// that the layers have been created and are up and running.
	//
  // The actual communication actors are depending on the protocol, and in
  // order to access functions on these classes, the pointers must be cast
  // to the pointers of the right class type. This is the purpose of the
  // following access function. It would be possible, but very complicated
  // to just pass the class and the function to be called on the class to a
  // caller function (which would be necessary had the pointers been unique
  // pointers) to ensure that the pointers could not be accidentally deleted
  // by the caller code.

public:

  template< class ServerClass >
  std::shared_ptr< ServerClass > Pointer( Layer Role )
  {
    std::shared_ptr< ServerClass > ThePointer =
	  std::dynamic_pointer_cast< ServerClass >( CommunicationActor[ Role ] );

    if ( ThePointer )
      return ThePointer;
    else
		{
			std::ostringstream ErrorMessage;

			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "Network server inheritance error";

			throw std::logic_error( ErrorMessage.str() );
		}

  }

  // ---------------------------------------------------------------------------
  // Constructor
  // ---------------------------------------------------------------------------
  //
  // The constructor first creates the layer servers by calling the given
  // network type's creator functions. These are properly initialised at the
  // time of calling since the endpoint has the network type as a base class.
  // Even though all functions are required, there is no guarantee that they
  // actually do call the network's create server function, and thus the
  // pointers to the various layers might not exist, or they are default
  // initialised. An extensive test is therefore carried out to ensure that
  // the network endpoint will be operational after construction. It should
  // be noted that this is just testing the validity of the various
  // network layer classes and not that they behave as expected for the given
  // network protocol to be used by the actor system.
  //
  // Given that the network type may require some additional parameters for
  // the technology dependent network functionality, it is possible to give
  // more parameters than the two required, and these are forwarded to the
  // network type constructor directly.

public:

 template< typename ... NetworkParameterTypes >
 NetworkEndpoint( const std::string & Name,
									NetworkParameterTypes && ... NetworkParameterValues )
  : Actor( Name ), StandardFallbackHandler( Actor::GetAddress().AsString() ),
    NetworkType( Actor::GetAddress().AsString(),
					 std::forward< NetworkParameterTypes >(NetworkParameterValues)... )
  {
    NetworkType::CreateNetworkLayer();
		NetworkType::CreateSessionLayer();
		NetworkType::CreatePresentationLayer();

		// The validity of these constructions is tested to ensure that a server
		// has been defined for each layer

		const std::map< Layer, std::string > LayerStrings = {
				{ Layer::Network, "network" },
				{ Layer::Session, "session" },
				{ Layer::Presentation, "presentation"}
		};

		for ( const auto & TheLayer : LayerStrings )
			try
			{
				auto Server = CommunicationActor.at( TheLayer.first );

				// Then a check that the server has been allocated

				if ( ! Server )
					throw std::out_of_range( "No valid network layer server" );
			}
			catch ( std::out_of_range & error )
			{
				std::ostringstream ErrorMessage;

				ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
										 << "Network end point: No server for the network layer "
										 << TheLayer.second
										 << " has been defined. Improper initialisation!";

			  throw std::logic_error( ErrorMessage.str() );
			}

		// All servers OK, so it makes sense to bind them

		NetworkType::BindServers();
  }

  // The destructor is also not doing anything particular since the managed
  // actors for the network layers will be destroyed by the unique pointer
  // destructor

  virtual ~NetworkEndpoint( void )
  { }
};

}  			// End namespace Theron
#endif  // THERON_TRANSPARENT_COMMUNICATION
