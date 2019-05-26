/*=============================================================================
Active Message Queue interface

The Active Message Queue (AMQ) [1] is a server based messaging system where
various clients can exchange messages via a server (message broker) using
the following two models of communication.

1. Publish-subscribe: A "topic" is defined on the message broker and clients
   in the system can subscribe to the topic and will receive the messages
   other clients publish to this topic.
2. Queue-to-workers: A queue held by the message broker and the subscribers
   receives the messages put to this queue in a round-robin way.

Implementing a distributed Theron++ actor system implies having actors on many
different endpoints, and any actor should be able to send a message to any
other actor. Essentially this calls for a peer-to-peer endpoint overlay network.
Furthermore, just like actors can be created and destroyed at run-time,
endpoints of the system may come and go. Thus there is a need for mechanism
that enables a new endpoint to join the communication graph of the other
endpoints.

A publish-subscribe model requires some tweaking for a dynamic peer-to-peer
system. A new endpoint can be directed to subscribe to one of the other peers'
publish channel, but when the owner of that channel has no knowledge about the
subscribers, it cannot subscribe back to establish a two-way communication with
the new endpoint, and tell the new endpoint about other peers it may know about.
This can be solved in a peer-to-peer system by having a pre-defined topic for
the endpoints. Ideally, this should be persisted so that new endpoints can just
subscribe to the topic and thereby receive all past messages sent to this topic.
However, this may require a special configuration of the message broker, and
it cannot be an assumption for the endpoint discovery. Instead, a very simple
protocol is being used: Each joining endpoint simply subscribes to a pre-defined
"TheronPlusPlus" topic on a given broker. This channel will be used for the
actor discovery protocol described next.

There are three approaches possible for the actor-to-actor communication, all
with their pros and cons:

I.   There is one common message channel where all endpoints post messages from
     their actors. This is conceptually simple, but all messages from all goes
     through this topic, and the messages arrives at all endpoint also those not
     having an actor with the destination identifier.
II.  Each endpoint has an associated actor message topic and publishes messages
     from its actors on this topic. Essentially this is the same as the first
     option, but endpoints with no communication will not need to subscribe.
     All subscribing endpoints gets all sent messages and receiver side filters
     must be applied to ignore messages for actors not on the current endpoint.
III. There is a destination actor discovery: The sending actor's endpoint sends
     a discovery message on the pre-defined channel. The endpoint hosting the
     destination actor sends a "hosting message" on the pre-defined
     discovery topic. When this is received by the sending endpoint, it will
     publish message on hold on the right endpoint-to-endpoint topic. This will
     make the data traffic endpoint peer-to-peer, and avoid flooding for the
     other endpoints, at the expense of a longer delay for the first message on
     an actor-to-actor communication pair.

The one-to-one actor-to-actor communication is application defined and based
on the actor names. Distributing the actor system across several endpoints
can be done to increase performance or for architectural reasons, but it is
still one actor system. Thus, there should be only one other endpoint
responding to an actor discovery message. The outgoing message is then published
to in-box of the endpoint of the destination actor. Consequently, each endpoint
subscribes to a AMQ topic having the endpoint name to receive messages for
actors running on that endpoint.

The implementation here follows the concept of the Theron++ transparent
communication over the following layers:

A. The network layer taking care of the AMQ specific parts. It initializes the
   AMQ library, connects to the server, and subscribes to two topics: The
   discovery channel "TheronPlusPlus" and the in-box for data messages with
   the same name as the endpoint.
B. The session layer mapping actors to external endpoint (topics). When
   receiving a discovery request it will check if the requested actor is local,
	 if it is a response will be sent, otherwise the message will be discarded.
	 When a discovery has been made, the external endpoint is cached as a topic
	 for the external actor. As actors can be created and destroyed, and nobody
	 needs to inform anyone about such events, the cache will be considered
	 valid only for a limited amount of time. By default one hour. If there has
	 been no messages sent to a remote actor for that amount of time, the
	 cached endpoint topic will be removed, and a new discovery must be done on
	 the next message for this remote actor.

However, the AMQ may also be used for data acquisition and used to distribute
messages from entities that are not remote actors. A receiving actor may
subscribe to any topic by sending a subscription message to the session layer.
The session layer will then request a subscription to be made by the network
layer, and messages received on these topics will have an empty sender actor.
The session layer will dispatch all these messages to the subscribing actors.
This implies that a subscribing actor should unsubscribe before closing to
avoid that further notification will be sent to a non-existing receiver actor.

IMPORTANT: Since the transparent communication of Theron++ assumes that the
messages sent and received are serialised, only text messages are supported in
the current implementation. It should be the job of the presentation layer to
implement a higher level protocol for serialising or de-serialising the arrived
string.

When this AMQ interface is used, the application must be linked with the
AMQ library and the SSL library is also necessary since the interface uses
Open SSL for encrypted communication (if needed) according to Kevin Boone [4]:

-lactivemq-cpp -lssl

References:

[1] http://activemq.apache.org/
[2] http://activemq.apache.org/cms/index.html
[3] http://activemq.apache.org/cms/cms-api-overview.html
[4] http://kevinboone.net/cmstest.html

Author and Copyright: Geir Horn, University of Oslo, 2018-2019
License: LGPL 3.0
==============================================================================*/

#ifndef THERON_ACTIVE_MESSAGE_QUEUE
#define THERON_ACTIVE_MESSAGE_QUEUE

// Standard headers

#include <string>     // Standard strings
#include <memory>     // Smart pointers

// Theron++ Actor Framework headers

#include "Actor.hpp"
#include "Utility/StandardFallbackHandler.hpp"

// Generic network endpoint

#include "Communication/NetworkEndpoint.hpp"

// This header file is the master file ensuring that all the class level
// headers are correctly included.

#include "Communication/AMQ/AMQMessages.hpp"
#include "Communication/AMQ/AMQNetworkLayer.hpp"
#include "Communication/AMQ/AMQSessionLayer.hpp"
#include "Communication/AMQ/AMQPresentationLayer.hpp"

namespace Theron::ActiveMQ
{
/*==============================================================================

 AMQ Network

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
  // Storing layer servers
  // ---------------------------------------------------------------------------
	//
	// This AMQ Network class is final class and it should not be further
	// inherited. The network layer actors can therefore be direct data member
	// of this class.

private:

	ActiveMQ::NetworkLayer      NetworkServer;
	ActiveMQ::SessionLayer      SessionServer;
	ActiveMQ::PresentationLayer PresentationServer;

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

	static Network * AMQNetwork;

	// Then there is a public function to obtain the addresses based on the
	// layer types defined in the standard network class. One could directly
	// return the value, however some compilers will issue a warning that
	// there is no return from the function, and the address is therefore
	// stored temporarily in the switch statement.

public:

	inline static Address GetAddress( Theron::Network::Layer Role )
	{
		Address LayerServer;

	  switch( Role )
	  {
			case Theron::Network::Layer::Network:
	      LayerServer = AMQNetwork->NetworkLayerAddress();
	      break;
			case Theron::Network::Layer::Session:
	      LayerServer = AMQNetwork->SessionLayerAddress();
	      break;
			case Theron::Network::Layer::Presentation:
	     LayerServer = AMQNetwork->PresentationLayerAddress();
	     break;
    }

    return LayerServer;
  }

  // As this function overshadows the similar function from the actor, the
  // actor function is explicitly reused (differences in argument lists is
  // enough for the compiler to distinguish the two variants.)

  using Actor::GetAddress;

  // ---------------------------------------------------------------------------
  // Shut down management
  // ---------------------------------------------------------------------------
	//
	// There is no special shut down management for AMQ networks. It is difficult
	// to see how it can be implemented since first of all the local actors on
	// this endpoint using the AMQ interface must be stopped.

  // ---------------------------------------------------------------------------
  // Constructor and destructor
  // ---------------------------------------------------------------------------
	//
	// The constructor must have an endpoint name, and it should be noted that
	// This is the external name to be used towards the remote AMQ endpoints
	// giving actor addresses like <actor name>@<endpoint name>. The endpoint
	// name is also the name of the Network Layer server - not the endpoint
	// actor. To avoid a name clash for the network server actors, an optional
	// endpoint prefix can be given. This defaults to "AMQ:" and so the
	// name of a server actor will be "AMQ:<servername>" and the endpoint
	// Network actor, the Session Layer server, and the Presentation layer
	// server will all receive this name.
	//
	// The IP address or the DNS lookup name for the AMQ server (or message
	// broker) must be given. Note that this does not specify the protocol as
	// the TCP will be added to this IP when connecting. An optional server port
	// can be given.
	//
	// Finally, the names for the Session Layer server and the Presentation Layer
	// server can be given. They are only used to create the named actors,
	// and default names are used if they are not given. The final argument to
	// the constructor is the optional endpoint prefix to be added to these
	// server names and to the network endpoint actor.

protected:

	Network( const std::string & EndpointName,
					 const std::string & AMQServerIP,
					 const std::string & AMQServerPort = "61616",
					 const std::string & SessionServer = "SessionLayer",
					 const std::string & PresentationServer = "PresentationLayer",
					 const std::string & AMQPrefix = "AMQ:" );

	// The default constructor and the copy constructor are deleted

	Network( void ) = delete;
	Network( const Network & Other ) = delete;

	// The destructor is virtual to ensure proper closing of base classes

	virtual ~Network()
	{}
};

/*==============================================================================

 AMQ Endpoint

==============================================================================*/
//
// Setting up the endpoint for AMQ implies simply to reuse the standard network
// endpoint for the above network class creating the right servers.

using NetworkEndpoint = Theron::NetworkEndpoint< Theron::ActiveMQ::Network >;

}      // End name space Theron AMQ
#endif // THERON_ACTIVE_MESSAGE_QUEUE
