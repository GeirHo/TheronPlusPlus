/*==============================================================================
Active Message Queue Session Layer

The general use use of the AMQ for connecting various peer endpoints hosting
Theron++ actors is described in the ActiveMQ.hpp header. The implementation
maps onto Theron++ communication architecture with a Network Layer actor
serving the technology specific protocol, a Session Layer described here, and
a Presentation Layer responsible for serializing messages that have receivers
on remote endpoints.

The Session Layer is responsible for keeping track of the addresses of external
actors receiving messages from actors on this endpoint. It is responsible of
informing the Session Layer actors on remote endpoints about new communicating
actors on this endpoint, and caches the external addresses for these local
actors. Thus the external address of a remote actor should, in theory, already
be available when a local actor sends a message to this remote actor. Should
this not be the case, then the Session Layer initiates an address resolution
transaction with the other peer endpoints and caches the messages for the
remote actor until one of the other endpoints confirm the external actor
address. When resolution requests arrives from remote endpoints, the session
layer looks up in its registry of local actors and initialises the resolution
response if the requested actor is hosted on this endpoint. All of this is
standard operation of the Session Layer independent of the underling
communication protocol and technology.

The AMQ Session Layer adds support for an actor to subscribe to other topics
on the AMQ server (message broker). The only requirement is that the
publishers on the topic send serialized AMQ text messages. This can typically
be topics for recorded sensor values that are not originating from the activity
of remote actors. As multiple local actors can subscribe to the same topic,
the Session Layer maintains these subscriptions and dispatches incoming messages
on the topic to all local actors subscribing to the topic. A subscription can
be lifted at any time if a local actor no longer wants messages from a given
topic.

References:

Author and Copyright: Geir Horn, University of Oslo, 2017-2019
License: LGPL 3.0
==============================================================================*/

#ifndef THERON_ACTIVEMQ_SESSION_LAYER
#define THERON_ACTIVEMQ_SESSION_LAYER

// Standard headers

#include <string>            // Standard strings
#include <unordered_map>     // For mapping subscribed topics to actors
#include <unordered_set>     // For storing topic subscribers

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

#include <activemq/library/ActiveMQCPP.h>
#include <activemq/core/ActiveMQConnectionFactory.h>
#include <cms/Connection.h>

// Headers for the Theron++ actor system

#include "Actor.hpp"
#include "Utility/StandardFallbackHandler.hpp"
#include "Communication/SerialMessage.hpp"
#include "Communication/SessionLayer.hpp"

// AMQ specific Theron++ classes

#include "Communication/AMQ/AMQMessages.hpp"
#include "Communication/AMQ/AMQNetworkLayer.hpp"

namespace Theron::ActiveMQ
{
/*==============================================================================

 Session Layer

==============================================================================*/
//
// The actor class encapsulating the Active MQ interface is performing tasks
// close to the role of the session layer of the OSI stack mapping external
// and internal communication layers and maintaining sessions.

class SessionLayer
: virtual public Actor,
  virtual public StandardFallbackHandler,
	virtual public Theron::SessionLayer< TextMessage >
{
protected:

	// ---------------------------------------------------------------------------
	// Server address management
	// ---------------------------------------------------------------------------
	//
  // The session layer communicates with the network layer and the presentation
	// layer and needs the addresses for these servers, which can be obtained
	// from the endpoint.

	virtual Address NetworkLayerAddress     ( void ) const final;
	virtual Address PresentationLayerAddress( void ) const final;

	// ---------------------------------------------------------------------------
	// Subscription management
	// ---------------------------------------------------------------------------
	//
	// The topics subscribed to by at least one actor on this endpoint are
	// stored in an unordered map so that they can easily be looked up. As
	// there can be many subscribers per topic, these are kept in a set of
	// actors to ensure that each actor only receives one copy of a message on
	// the topic.

private:

	std::unordered_map< std::string, std::unordered_set< Address > >
	Subscriptions;

	// Actors on this endpoint subscribes to topics by using the messages
	// defined in the network layer. However, to ensure that these are seen as
	// session layer defined messages by the actors, they are private in the
	// Network Layer server, and redeclared as public here.

public:

	using CreateSubscription = NetworkLayer::CreateSubscription;
	using CancelSubscription = NetworkLayer::CancelSubscription;

	// The corresponding handlers basically maintains the subscription map of
	// active subscriptions. If an arriving subscription is for a topic or queue
	// not yet subscribed, then a subscription request is sent to the network
	// layer. Otherwise, the actor is just added to the subscriber set for an
	// existing subscription.

private:

	void NewSubscription( const CreateSubscription & Request,
												const Address From );

	// The handler removing subscriptions are basically reversing the
	// subscription requests, and when the last actor subscribing to a topic or
	// queue deletes the subscription, then a notification is sent to the
	// Network Layer that the corresponding message listener can be shut down.

	void RemoveSubscription( const CancelSubscription & Request,
													 const Address From );

	// When the session layer is requested to stop it will force a removal of
	// all active subscriptions, and hence it needs to override the handler
	// for the stop messages.

	virtual void Stop( const Network::ShutDown & StopMessage,
										 const Address Sender ) override;

	// ---------------------------------------------------------------------------
	// Inbound messages
	// ---------------------------------------------------------------------------
	//
	// The overloaded version of the inbound message handler checks if the
	// arriving message has a receiver actor name or not. If the name is empty,
	// it means that the message comes from a topic subscribed to and not from
	// actors on remote endpoints. It will then be forwarded to all actors
	// subscribing to this topic with the topic name as the sender actor. It is
	// the responsibility of the receiving actor never to respond to such a
	// message as it will just be held unsent by the session layer.

protected:

	virtual void InboundMessage( const TextMessage & TheMessage,
										           const Address TheNetworkLayer ) override;

 	// ---------------------------------------------------------------------------
	// Constructor and destructor
	// ---------------------------------------------------------------------------
	//
  // The only thing that is needed to create the AMQ session layer is the
  // actor name, although it does have a default name for this endpoint as
  // there should be only one AMQ session layer per endpoint.

public:

  SessionLayer( const std::string & ServerName = "AMQSessionLayer" );

	// The destructor will ask the network layer to remove all the special topic
	// or queue listeners created if there are still subscriptions active that
	// have not been cleared by the endpoint actors. By definition this should
	// not be necessary as closing the Session Layer should be followed by
	// closing the Network Layer which will again close all listeners and stop
	// the connection with the AMQ Broker. Hence, this is just done as a
	// precaution to ensure no further errors.

	virtual ~SessionLayer();
};



}      // Name space ActiveMQ
#endif // THERON_ACTIVEMQ_SESSION_LAYER
