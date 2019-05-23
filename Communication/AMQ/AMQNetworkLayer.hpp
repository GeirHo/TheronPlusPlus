/*==============================================================================
Active Message Queue: Network Layer

The Active Message Queue (AMQ) [1] is a server based messaging system where
various clients can exchange messages via a server (message broker) using
the following two models of communication. See the ActiveMQ header file for
details on implementation.

The Network Layer takes care of the interface to the AMQ server (broker). It
initialises the AMQ library and connects to the server. It subscribes to a
AMQ with the same topic as the endpoint name. This is the in-box for this
endpoint. It also publishes actor discovery messages on the pre-defined topic
"TheronPlusPlus". All endpoints subscribes to this topic, and inbound messages
will be forwarded as discovery messages to the Session Layer.

Finally, it supports raw topic subscriptions for messages not sent by remote
actors. Messages arriving on these topics are forwarded to the Session Layer
with an unknown sender. Such subscriptions can also be cancelled by the
session layer when no local actor subscribes to the given topic any more.

References:

[1] http://activemq.apache.org/

Author and Copyright: Geir Horn, University of Oslo, 2018-2019
License: LGPL 3.0
==============================================================================*/


#ifndef THERON_AMQ_NETWORK_LAYER
#define THERON_AMQ_NETWORK_LAYER

// Standard headers

#include <memory>        // For smart pointers
#include <string>        // For standard strings
#include <map>           // For storing properties
#include <unordered_map> // For O(1) lookups
#include <set>           // For storing subscribing actors
#include <typeinfo>      // For knowing property types
#include <typeindex>     // For storing the property types
#include <sstream>       // For nice error messages
#include <stdexcept>     // For standard exceptions
#include <functional>    // Dynamic functions

// Headers for the Active Message Queue interface and the C++ Messaging System
// (CMS) [2]. Unfortunately, the C++ library for AMQ is written to be similar
// to the Java interface and it therefore suffers from Java or C-style ways of
// using the various classes and functions. It also passes message and object
// pointers around without clarifying the ownership or using shared pointers.
// This interface is made more C++ like in this implementation, and pointers
// to CMS objects are always protected by smart pointers if they are created
// within the scope of this implementation.
//
// Note that the location of these files my be non-standard and the current
// version is under
// /user/include/activemq-cpp-3.9.4

#include <activemq/core/ActiveMQConnectionFactory.h>
#include <activemq/core/ActiveMQConnection.h>
#include <activemq/core/ActiveMQSession.h>
#include <activemq/cmsutil/CachedConsumer.h>
#include <activemq/cmsutil/CachedProducer.h>
#include <activemq/library/ActiveMQCPP.h>

#include <cms/Connection.h>
#include <cms/Session.h>
#include <cms/MessageListener.h>

// Headers for the Theron++ actor system

#include "Actor.hpp"
#include "Utility/StandardFallbackHandler.hpp"

#include "Communication/NetworkLayer.hpp"
#include "Communication/AMQ/AMQMessages.hpp"

namespace Theron::ActiveMQ
{

// Setting the hard coded topic for all endpoints to listen to.

constexpr char DiscoveryTopic[] = "TheronPlusPlus";

/*==============================================================================

 Network layer

==============================================================================*/
//
// The network layer implements most of the AMQ specific issues ranging from the
// initialisation of the protocol to the format conversion for the messages.
// It receives outbound messages for a given topic from the session layer,
// and forwards received messaged on a topic to the session layer which
// maintains a mapping of actors subscribing to a particular topic.

class NetworkLayer
: virtual public Actor,
  virtual public StandardFallbackHandler,
  public Theron::NetworkLayer< TextMessage >
{
 	// ---------------------------------------------------------------------------
	// Connectivity
	// ---------------------------------------------------------------------------
	//
	// In order to connect to the AMQ server a connection and a session are
	// necessary.

private:

	activemq::core::ActiveMQConnection * AMQConnection;
	activemq::core::ActiveMQSession    * AMQSession;

  // ---------------------------------------------------------------------------
	// Handling AMQ Messages
	// ---------------------------------------------------------------------------
	//
	// The messages from other actors to actors on this endpoint is forwarded to
	// the session layer for further processing. Since only serialised text
	// messages are assumed, messages of other types of AMQ messages will lead
	// to a run time error exception.

private:

	void InboundMessage( const cms::Message * TheMessage );

	// Outbound messages arrives from the Session layer server with the topic set
	// to the remote endpoints in-box. Thus the handler will just publish the
	// message on that queue.

protected:

	virtual void OutboundMessage( const TextMessage & TheMessage,
																const Address From ) override;

	// A command message arrives on the discovery topic when a remote
	// endpoint wants to send a message to an actor whose location is unknown.
	// Thus these requests should be ignored by endpoints not hosting the actor
	// and a reply should include the endpoint name.
	//
  // The currently devised AMQ protocol detects destination actors when another
  // actor tries to send a message to this actor. If the destination actor
  // does not exist, i.e. there are no positive responses to the resolution
  // request, it is undefined what to do and the session layer should cache
	// the outbound message waiting for the actor to appear.
	//
	// However, there is no obligation on an actor to register with the session
	// layer when it starts up, and there is no guarantee that a particular
	// remote actor ever will start up. The only possible protocol must therefore
	// be to retry the discovery request later. The session layer will do this
	// for pending discovery requests every time it needs to do a new discovery.
	// Yet, it is possible to push the discovery response if the remote actor
	// registers with the session layer and the session layer pushes a discovery
	// request for the local actor, see the handler function below.

private:

	enum class Command : short int
	{
		DiscoveryRequest  = 1,
		DiscoveryResponse = 2,
		ActorRemoved      = 3,
		EndpointShutDown  = 4
	};

	// The handler for this receives the corresponding CMS message and responds
	// if the actor is on this endpoint.

	void CommandHandler( const cms::Message * TheMessage );

	// ---------------------------------------------------------------------------
	// Session layer interaction
	// ---------------------------------------------------------------------------
	//
	// A remote actor discovery is initiated by the session layer when a local
	// actor registers or when a local actor wants to send a message to an
	// external actor.
	//
	// A local actor must be a de-serializing actor in order to receive messages
	// from the network. When these actors are created, they should register with
	// the session layer. The session layer will then pro-actively request the
	// external address of this actor from the network layer and store the
	// mapping between the global address and the internal actor address. In this
	// case a discovery response is generated to the other endpoints which may
	// be waiting for this actor to become available; i.e. there may be unhanded
	// discovery requests pending.
	//
	// The second situation occurs when a local actor wants to send a message
	// to a remote actor. Then an address resolution request for the remote actor
	// is sent and then the network layer must engage with network layers in a
	// protocol to verify the external address of the remote actor.

protected:

	virtual void ResolveAddress( const ResolutionRequest & TheRequest,
														   const Address TheSessionLayer ) override;

  // When a resolution request arrives from the network it should be sent to
  // the session layer that will respond with a resolution response if the
  // actor exists on this endpoint. The resolved address will then publish
  // this response on the discovery topic.

  virtual void ResolvedAddress( const ResolutionResponse & TheResponse,
																const Address TheSessionLayer ) override;

 	// The general view on communication is that an actor needs an external
	// address before it can send a packet because responses will be delivered
	// to this external address. Thus, when a local actor starts up it may
  // register with the session layer which will in turn request the external
  // address for this local actor using the resolve address method. In this
  // case the external address may be created by the network layer encoding
  // its own network location into the address. For push technologies, this
  // may imply that the new actor address is pushed to the remote endpoints so
  // they are aware of the new actor. In this case it would be a need to inform
  // remote endpoints when the actor disappears. For this reason a standard
  // handler is provided to inform the network layer that a local actor is
  // closing and that its external address can be invalidated.

  virtual void ActorRemoval( const RemoveActor & TheCommand,
														 const Address TheSessionLayer ) override;

	// ---------------------------------------------------------------------------
	// Receiving and sending AMQ messages
	// ---------------------------------------------------------------------------
	//
	// An AMQ topic or queue is a termed a destination, and it is possible both
	// to subscribe to messages from a destination, or send messages to a
	// destination. There is a small utility function to create the destinations
  // depending on the type of the destination (topic or queue).
private:

	using DestinationPointer = std::shared_ptr< cms::Destination >;

	DestinationPointer CreateDestination( const std::string & TopicOrQueue,
																        ActiveMQ::Message::Destination Type );

	// A producer is created on a destination and can be used to send messages
	// to that destination only. Hence there is a map of producers associated
	// with the queues or topics this endpoint can send messages to.

	std::unordered_map< std::string,
	                    std::unique_ptr< activemq::cmsutil::CachedProducer > >
  Producers;

	// Producers are created with the same arguments as above and directly
	// inserted into the map.

	void CreateProducer( const std::string & TopicOrQueue,
							         ActiveMQ::Message::Destination Type  );

	// Listening to events from arriving messages is different since the message
	// events will arrive asynchronously and the handling will be done by a
	// dedicated class that can provide a dedicated function for handling the
	// arrived message. The standard handler onMessage simply calls a handler
	// specified at the construction to support different handlers for different
	// destinations.

	class MessageMonitor : public cms::MessageListener
	{
	private:

	  std::unique_ptr< activemq::cmsutil::CachedConsumer > Monitor;

		std::function< void( const cms::Message * TheMessage ) > Handler;

		// The pre-defined message handler just forwards the message pointer to
		// the handler for processing.

		virtual void onMessage( const cms::Message* TheMessage ) override
		{	Handler( TheMessage ); }

		// The constructor initialises the listener and sets the handler based
		// on the given handler function and start the monitor to listen to the
		// topic. The handler is supposed to be a lambda function since it will
		// invoke a member function on the network layer class.

	public:

		template< class LambdaFunction >
		MessageMonitor( cms::Session * TheSession,
										const DestinationPointer & Destination,
										const LambdaFunction & HandlerFunction )
		: Monitor(), Handler( HandlerFunction )
		{
		  Monitor = std::make_unique< activemq::cmsutil::CachedConsumer >(
								  TheSession->createConsumer( Destination.get() ) );
			Monitor->setMessageListener( this );
			Monitor->start();
		}

		// There is no default constructor that can be accidentally used.

		MessageMonitor( void ) = delete;

		// The destructor stops the monitor before it is automatically deleted.

		~MessageMonitor( void )
		{ Monitor->stop(); }
	};

  // The message monitors are kept in the same sort of map from the topic or
	// queue identifier to a unique pointer for the monitor class. However, these
	// pointers will be created when a new subscription is made and deleted when
	// the subscription is cancelled. Thus these message handers maintain the
	// active monitors and stops the one for which the subscription is cancelled.

	std::unordered_map< std::string, std::unique_ptr< MessageMonitor > >
	Subscriptions;

	// ---------------------------------------------------------------------------
	// Actor managed subscriptions
	// ---------------------------------------------------------------------------
	//
  // It is easy to imagine that the AMQ server (broker) could be used for other
	// purposes for some applications. One example is when there are other
	// applications running providing sensor values and measurements to the actors
	// of the distributed actor system. Then the receiving actors need to manage
	// the subscriptions themselves. This is done by sending a subscribe request
	// to the session layer, and the session layer will then send a subscribe
	// request to the network layer. This is important because there need not be
	// a one-to-one mapping between subscriptions and actors. There could be many
	// actors subscribing to the same topic, and one actor may subscribe to many
	// topics.
	//
	// Once a topic has been subscribed to, it will be a viable destination and
	// it is also possible to send messages to the same topic. Incoming messages
	// will be managed exactly as messages on the in-box and just forwarded to
	// the session layer for distribution.

	class CreateSubscription
	{
	public:

		const std::string                    TopicOrQueue;
		const ActiveMQ::Message::Destination ChannelType;

		CreateSubscription( const std::string NameOfChannel,
												const ActiveMQ::Message::Destination Type )
		: TopicOrQueue( NameOfChannel ), ChannelType( Type )
		{}

		CreateSubscription( const CreateSubscription & Other )
		: TopicOrQueue( Other.TopicOrQueue ), ChannelType( Other.ChannelType )
		{}

		CreateSubscription( void ) = delete;
	};

	// There is a handler for this message that creates the destination and sets
	// up the subscription.

	void NewSubscription( const CreateSubscription & Request,
												const Address From );

	// Cancelling a subscription follows the same pattern where the session layer
	// sends a cancel subscription message. In this case only the channel name
	// is needed.

	class CancelSubscription
	{
	public:

		const std::string TopicOrQueue;

		CancelSubscription( const std::string & NameOfChannel )
		: TopicOrQueue( NameOfChannel )
		{}

		CancelSubscription( const CancelSubscription & Other )
		: TopicOrQueue( Other.TopicOrQueue )
		{}

		CancelSubscription( void ) = delete;
	};

	// Given that the actor system is asynchronous there may be incoming messages
	// on this queue over the time epoch from the moment the session layer sends
	// the cancellation request until it has been implemented by the network
	// layer. The handler for the cancel subscription will therefore return the
	// cancel subscription message back to the session layer (sender) to notify
	// that this topic is now completely removed.

	void RemoveSubscription( const CancelSubscription & Request,
													 const Address From );

	// Furthermore, there is no information about the format of the messages on
	// the subscribed queue or topic. Arriving messages can therefore not be
	// supposed to follow the above format with sender and destination encoded
	// in the message properties. There is consequently a special handler for
	// these messages taking the topic as the second argument and using this
	// as the sender actor name.

	void SubscriptionHandler( const std::string TopicOrQueue,
														const cms::Message * TheMessage );

	// The above messages can be sent from the session layer and only from the
	// session layer. They are therefore declared as private, and the session
	// layer is given special privileges.

	friend class SessionLayer;

	// ---------------------------------------------------------------------------
	// Shut down management
	// ---------------------------------------------------------------------------
	//
	// When a shut down message is received from the session layer it means that
	// all the local actors have disconnected. The shut down handler will then
	// just inform the other AMQ peers that this endpoint is shutting down, and
	// then stop the connection.

	virtual void Stop( const Network::ShutDown & StopMessage,
										 const Address Sender ) override;

	// ---------------------------------------------------------------------------
	// Constructor and destructor
	// ---------------------------------------------------------------------------
	//
	// The constructor takes the global identifier of this network endpoint as
	// its name and the IP of the server to connect to and optionally the port
	// used to reach the AMQ message broker. If no port is given, the standard
	// port 61616 will be used.

public:

	NetworkLayer( const std::string & EndpointName,
								const std::string & AMQServerIP,
							  const std::string & Port = "61616" );

	// The default constructor is deleted

	NetworkLayer( void ) = delete;

	// The destructor closes the connection and stops the AMQ library

	virtual ~NetworkLayer( void );
};

}      // End of name space Active MQ
#endif // THERON_AMQ_NETWORK_LAYER
