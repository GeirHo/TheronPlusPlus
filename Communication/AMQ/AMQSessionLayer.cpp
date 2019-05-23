/*==============================================================================
Active Message Queue session layer

Please see the details in the associated header file.

Author and Copyright: Geir Horn, 2017
License: LGPL 3.0
==============================================================================*/

#include "Communication/AMQ/AMQSessionLayer.hpp"

/*==============================================================================

 Message handling

==============================================================================*/
//
// The inbound messages could either come from remote actors for which both
// the actors' global sender addresses are known, and the default message
// lookup of the standard Session Layer can be used; or the message is from a
// topic or queue subscribed to by one or more local actors, and the message
// is forwarded to these destination actors via the presentation layer.

void Theron::ActiveMQ::SessionLayer::InboundMessage(
	const Theron::ActiveMQ::TextMessage & TheMessage,
	const Theron::Actor::Address TheNetworkLayer )
{
  if ( TheMessage.GetRecipient().ActorName().empty() )
	{
		// This message is from a topic subscribed to and not from a remote actor.
		// It should therefore be forwarded to all local actors subscribing to
		// this topic.

		std::string TopicOrQueue = TheMessage.GetSender().ActorName();
		auto Subscribers         = Subscriptions.find( TopicOrQueue );

		if ( Subscribers != Subscriptions.end() )
		{
			Address Sender( TopicOrQueue );
			SerialMessage::Payload ThePayload( TheMessage.GetPayload() );

			for ( const Address & TheSubscriber : Subscribers->second )
				Send( RemoteMessage( Sender, TheSubscriber, ThePayload ),
					    Network::GetAddress( Network::Layer::Presentation ) );
		}
	}
	else
		Theron::SessionLayer< TextMessage >::InboundMessage( TheMessage,
																												 TheNetworkLayer );
}

// Setting up new subscriptions implies creating a record for the subscription
// and adding the actor to the list of subscribers. If this was the first
// actor subscribing, the Network Layer must be notified so that it can set up
// a listener for the given topic.

void Theron::ActiveMQ::SessionLayer::NewSubscription(
	const Theron::ActiveMQ::SessionLayer::CreateSubscription & Request,
	const Theron::Actor::Address From )
{
	auto NovelSubscription = Subscriptions.emplace( Request.TopicOrQueue, From );

	NovelSubscription.first->second.emplace( From );

	// If this was the first actor subscribing then the request is forwarded to
	// the network layer.

	if ( NovelSubscription.second == true )
		Send( Request, Network::GetAddress( Network::Layer::Network ) );
}

// Removing the subscription necessitates first to find if there is a
// subscription for the given topic or queue name, and then if the requesting
// actor is one of the subscribers. If the subscriber set is empty after the
// removal of the requesting actor's address, then the network layer is
// asked to remove the message listener for this topic or queue.

void Theron::ActiveMQ::SessionLayer::RemoveSubscription(
	const Theron::ActiveMQ::SessionLayer::CancelSubscription & Request,
	const Theron::Actor::Address From)
{
	auto Subscribers = Subscriptions.find( Request.TopicOrQueue );

	if ( Subscribers != Subscriptions.end() )
  {
		Subscribers->second.erase( From );

		if ( Subscribers->second.empty() )
		{
			Send( Request, Network::GetAddress( Network::Layer::Network ) );
			Subscriptions.erase( Subscribers );
		}
	}
}

// Stopping the communication means that the network layer server should be
// asked to cancel all subscriptions, and then the subscriptions are removed
// before the base class shut down protocol is started.

void Theron::ActiveMQ::SessionLayer::Stop(
	const Network::ShutDown & StopMessage, const Theron::Actor::Address Sender )
{
	for ( auto & ActiveSubscription : Subscriptions )
  {
		CancelSubscription Cancellation( ActiveSubscription.first );
		Send( Cancellation, Network::GetAddress( Network::Layer::Network ) );
	}

	Subscriptions.clear();

	Theron::SessionLayer< TextMessage >::Stop( StopMessage, Sender );
}

/*==============================================================================

 Constructor and destructor

==============================================================================*/
//
// The constructor initializes the base classes and registers the message
// handlers that are specific to the AMQ layer. The virtual handlers are
// already registered by the base classes.

Theron::ActiveMQ::SessionLayer::SessionLayer(
	const std::string & ServerName )
: Actor( ServerName ), StandardFallbackHandler( ServerName ),
  Theron::SessionLayer< TextMessage >( ServerName ),
  Subscriptions()
{
	RegisterHandler( this, &SessionLayer::NewSubscription    );
	RegisterHandler( this, &SessionLayer::RemoveSubscription );
}

// The destructor simply closes the open subscriptions if there are any.

Theron::ActiveMQ::SessionLayer::~SessionLayer()
{
	for ( auto & Subscription : Subscriptions )
		Send( CancelSubscription( Subscription.first ),
					Network::GetAddress( Network::Layer::Network ) );
}

