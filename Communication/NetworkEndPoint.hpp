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

#include <iostream> 				//TEST

#include "Actor.hpp"			 // The Theron++ actor framework
#include "Utility/StandardFallbackHandler.hpp"

namespace Theron {

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
: virtual public Actor
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
	friend class NetworkEndPoint;

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
  // Domain
  // ---------------------------------------------------------------------------
  //
  // The location of this end point is stored for future reference by
  // communication specific derived classes, if this is needed at the time of
  // construction

  const std::string Domain;

	// ---------------------------------------------------------------------------
  // Shut down management
  // ---------------------------------------------------------------------------
  //
  // The network end point must provide a handler for a shut down message

public:

	class ShutDownMessage
	{
	public:

		ShutDownMessage( void )
		{ }

	};

protected:

	// Derived classes must provide a handler for shut down messages and make
	// sure that the network closes properly when this message is received.

	virtual void StartShutDown( const Network::ShutDownMessage & TheMessage,
														  const Address Sender ) = 0;

  // ---------------------------------------------------------------------------
  // Constructor and destructor
  // ---------------------------------------------------------------------------
	//
  // The default constructor is made protected to ensure that only the network
	// endpoint can call it, and hence any derived class cannot be instantiated
	// even though it defines the proper virtual functions. The copy constructor
	// is explicitly deleted for the same reasons.

	Network( const std::string & Name, const std::string & Location )
	: Actor( Name ), Domain( Location )
	{
		ThisNetworkEndpoint = this;
		RegisterHandler( this, &Network::StartShutDown );
	}

	Network( void ) = delete;
	Network( const Network & Other ) = delete;

	// The destructor is public and virtual to allow the proper destruction of
	// the derived classes

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
class NetworkEndPoint
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
  // Shut down management
  // ---------------------------------------------------------------------------
  // A small problem in Theron is that there is no function to wait for
  // termination. Each actor is in principle a state machine that will run
  // forever exchanging messages. The thread that starts main and creates the
  // network endpoint and the actors should be suspended until the actor system
  // itself decides to terminate. The correct way to do this is to use a
  // Receiver object that will get a last message when all the key actors in
  // the system no longer have messages to process. Thus when this receiver
  // terminates its wait, the main() will probably terminate calling the
  // destroying the objects created within main().
  //
  // In larger systems there will be more actors than threads and, perhaps, more
  // threads than cores. The result is that some threads may be waiting for an
  // available core and some actors may be waiting for available threads. Thus,
  // when the deciding actor has entered the finishing state, there may still
  // be unhanded messages for other actors, and some of these may be processed
  // in other threads while main() is destroying the actors almost guaranteeing
  // that the application will terminate with a segmentation fault.
  //
  // Given that there is no active interface to the Theron scheduler, it is not
  // possible to know when there are no pending messages. However, if the actors
  // created in main() are known, then it is possible to verify that all their
  // message queues are empty before terminating.
  //
  // This idea is implemented with a Receiver that has a set of pointers to
  // actors to wait for. When it receives the shut down message it will loop
  // over the list of actors checking if they have pending messages, and if so
  // it will suspend execution for one second. The handler will only terminate
  // when there are no pending messages for any of the guarded actors.
  //
  // There are two methods provided by the Network End Point to use this
  // Receiver: One Wait For Termination that creates the receiver and waits
  // for it to receive the shut down message, and one Shut Down method that
  // can be called by the main actor when the system is ready for shut down
  // as the message exchange dries up.

private:

	class TerminationReceiver : public Receiver
	{
	private:

		std::set< const Actor * > GuardedActors;

		// The termination message handler

		void StartTermination( const Network::ShutDownMessage & TheRequest,
													 const Address Sender )
		{
			while ( std::any_of( GuardedActors.begin(), GuardedActors.end(),
										 [](const Theron::Actor * TheActor)->bool{
											  return TheActor->GetNumQueuedMessages() > 0;  }) )
				std::this_thread::sleep_for( std::chrono::seconds(1) );
		}

	public:

		// The constructor takes an initialisation list to ensure that all pointers
		// are for actors.

		TerminationReceiver(
									const std::initializer_list< const Actor * > & ActorsToGuard )
		: GuardedActors( ActorsToGuard )
		{
			RegisterHandler( this, &TerminationReceiver::StartTermination );
		}

	};

	// There is a shared pointer for this termination receiver to ensure that it
	// is properly destroyed and so that it can be passed outside if someone
	// wants to manage the sending of the termination message directly.

	std::shared_ptr< TerminationReceiver > ShutDownReceiver;

	// There is a public interface function to send the shut down message to this
	// receiver. It optionally takes the address of the calling actor and sends
	// the message as if it was from that actor. It throws if there is no shut
	// down receiver allocated.

	void StartShutDown( const Network::ShutDownMessage & TheMessage,
										  const Address Sender )
	{
		if ( ShutDownReceiver )
			Send( Network::ShutDownMessage(),
						Sender, ShutDownReceiver->GetAddress() );
		else
		{
			std::ostringstream ErrorMessage;

			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "Sending a shut down message requires a shut down "
									 << "receiver";

			throw std::logic_error( ErrorMessage.str() );
		}
	}

public:

	// The shut down receiver and object is created and obtained by a creator
	// function that takes a list of actors that can be converted to actor
	// pointers and passed to the termination receiver. Note that it adds the
	// network layer servers to the set of actors to look after.
	//
	// Note: There is deliberately no Wait function as this has to be called
	// on the object returned by this function, i.e. by invoking the standard
	// wait function on the receiver.
	//
	// Implementation note: The initialiser list must be given as an initialiser
	// in stead of the constructor, and the compiler will call the correct
	// constructor. In other words, if the initialiser list constructor should
	// be called, one would need to say T{...} and not T({...}), and the
	// standard make shared will implicitly use the latter case. The alternative
	// could be to template the constructor, but then it would not be as easy to
	// ensure that the types of the given arguments were really pointers to actors

	template< class... ActorTypes >
	std::shared_ptr< TerminationReceiver >
	TerminationWatch( ActorTypes & ...TheActor )
	{
		ShutDownReceiver = std::shared_ptr< TerminationReceiver >(
			new TerminationReceiver
				  { CommunicationActor[ Layer::Network 			].get(),
						CommunicationActor[ Layer::Session 			].get(),
						CommunicationActor[ Layer::Presentation ].get(),
						dynamic_cast< const Actor * >( &TheActor )...
					}
		);

		return ShutDownReceiver;
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
 NetworkEndPoint( const std::string & Name, const std::string & Location,
									NetworkParameterTypes && ... NetworkParameterValues )
  : Actor( Name ), StandardFallbackHandler( Actor::GetAddress().AsString() ),
    NetworkType(
	    Name, Location,
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
  // actor will be destroyed by the unique pointer destructor

  virtual ~NetworkEndPoint( void )
  { }
};

}  			// End namespace Theron
#endif  // THERON_TRANSPARENT_COMMUNICATION
