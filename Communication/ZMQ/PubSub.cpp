/*=============================================================================
Zero Message Queue

This file implements the Zero Message Queue interface for Theron++. Please see
the corresponding header for details, and the references.

Author and Copyright: Geir Horn, 2017
License: LGPL 3.0
=============================================================================*/

#include <map>						              // To match types and strings
#include <list>                         // To store lists of IP addresses
#include <set>                          // To store unique active connections
#include <algorithm>                    // To search for strings
//#include <cstdio>												// For sscanf
#include <stdexcept>										// Standard exceptions
#include <sstream>											// For error reporting

#include "ZeroMQ.hpp"                   // ZMQ interface definition




/*==============================================================================

 Publishing

==============================================================================*/
//
// When a subscriber wants to subscribe or unsubscribe the event handler is
// called. Only subscriptions are processed for now, and if the topic
// wanted by the subscriber is in the cache, it will be published again trusting
// that other subscribers are able to filter out this double posting.

void Theron::ZeroMQ::PublishingLink::SubscriptionEvents( void )
{
	enum class EventType : unsigned short int
	{
		Unsubscribe = 0,
		Subscribe   = 1
	};

	// Read the event from the socket

	zmqpp::message TheEvent;
	DataPublisher.receive( TheEvent );

	// Parsing the event

	unsigned short int Command;
	std::string EventString, Topic;

	TheEvent >> EventString;
	std::istringstream EventRecord( EventString );

	EventRecord >> Command >> std::ws >> Topic;

	// Then check the cache and re-publish the last known value if this was a
	// subscription. If the topic is unknown, nothing will be done by this node
	// as the subscription was probably made before any actor published on this
	// topic so the subscriber just have to wait.

	if ( Command == static_cast< unsigned short int>( EventType::Subscribe ) )
  {
		auto ThePublisher = Publishers.find( Topic );

		if ( ThePublisher != Publishers.end() )
			Send( SendLastGoodValue( Topic ), ThePublisher->second );
	}
}

// -----------------------------------------------------------------------------
// Message handlers
// -----------------------------------------------------------------------------
//
// When the publisher receives the command to send the cached value, it will
// check if there is a value for this topic, and if so, send it as a normal
// publishable message to be dispatched by the publishing link.

void Theron::ZeroMQ::Publisher::NewSubscription(
	const PublishingLink::SendLastGoodValue & Broadcast, const Address TheLink)
{
	auto ValueRecord = LastGoodValue.find( Broadcast.Topic );

	if ( ValueRecord != LastGoodValue.end() )
		Send( PublishingLink::PublishableMessage( Broadcast.Topic,
																							ValueRecord->second ), TheLink );
}

// Messages will come from actors in terms of a message that already contains
// the serialised payload.

void Theron::ZeroMQ::PublishingLink::DispatchMessage(
		 const PublishableMessage & TheMessage, const Address ThePublisher )
{
	// Publishing the message

	zmqpp::message LinkMessage;

	LinkMessage << TheMessage.Topic << TheMessage.Payload;
	DataPublisher.send( LinkMessage );

	// Then storing the address of the actor owning this topic. If the topic is
	// known already the actor address will be overwritten by itself, but this
	// is probably as efficient as testing for this case.

	Publishers[ TheMessage.Topic ] = ThePublisher;
}

// When a publisher closes, all its topics should be deleted. Given that the
// unordered map keeps the relative order of the elements when an element is
// deleted, the lookup on the value field, i.e. the address of the publisher as
// the second field of an element in the map, can be done in one iteration.

void Theron::ZeroMQ::PublishingLink::DeletePublisher(
	const ClosePublisher & TheCommand, const Address ThePublisher )
{
	auto PublisherRecord = Publishers.begin();

	while ( PublisherRecord != Publishers.end() )
		if( PublisherRecord->second == ThePublisher )
			PublisherRecord = Publishers.erase( PublisherRecord );
		else
			++PublisherRecord;
}

// -----------------------------------------------------------------------------
// Constructors and destructor
// -----------------------------------------------------------------------------
//

Theron::ZeroMQ::PublishingLink::PublishingLink(
	const IPAddress & InitialRemoteEndpoint,
	const std::string & RemoteLinkServerName, const std::string & ServerName)
: Actor( ServerName ), StandardFallbackHandler( GetAddress().AsString() ),
  Link( InitialRemoteEndpoint, RemoteLinkServerName, GetAddress().AsString() ),
  DataPublisher( GetContext(), zmqpp::socket_type::xpublish )
{
	DataPublisher.set( zmqpp::socket_option::xpub_verbose, true );
	DataPublisher.bind( TCPAddress( GetNetworkAddress().GetIP(),
																	DataPort ).AsString() );
	GetMonitor().add( DataPublisher,
										[this](void)->void{ SubscriptionEvents(); } );

	RegisterHandler( this, &PublishingLink::DispatchMessage );
	RegisterHandler( this, &PublishingLink::DeletePublisher );
}

// The publisher's constructor simply initialises the cache and register the
// hander for new subscriptions.

Theron::ZeroMQ::Publisher::Publisher(const std::string Name)
: Actor( Name ), StandardFallbackHandler( GetAddress().AsString() ),
  LastGoodValue()
{
	RegisterHandler( this, &Publisher::NewSubscription );
}

// The publisher's destructor must inform the publishing link that it closes

Theron::ZeroMQ::Publisher::~Publisher()
{
	Send( PublishingLink::ClosePublisher(),
				Network::GetAddress( Network::Layer::Network ) );
}

/*==============================================================================

 Subscriber actor

==============================================================================*/
//
// When a subscription is made for a message from an actor, a request for
// finding the network endpoint will be sent to the local network layer server,
// which may optionally involve remote network layer servers and their session
// layer servers to find the publishing actor's location. The response from
// the link layer server at the network endpoint hosting the actor will be
// returned as a resolution response message.
//
// The handler for this message type will then connect to the endpoint that
// has the publishing actor, and then subscribe to all the topics corresponding
// to different message types that local actors subscribe to.

void Theron::ZeroMQ::Subscriber::ConnectSubscription(
		 const Link::ResolutionResponse & ResolvedAddress,
		 const Address SessionLayerServer)
{
	Subscription.connect( ResolvedAddress.GlobalAddress.GetEndpoint() );

	// Find the range of topics pending for the publishing actor and subscribe to
	// these topics.

	auto Range = PendingRequests.equal_range( ResolvedAddress.TheActor );

	// if there are elements in the range, the actor name and the endpoint ID
	// string are cached as they will be needed for each message type subscribed
	// to from this remote publishing actor.

	if ( Range.first != Range.second )
  {
		std::string ActorName ( ResolvedAddress.TheActor.AsString() ),
								EndpointID( ResolvedAddress.GlobalAddress.GetIP().to_string() );

		auto aSubscription = Range.first;

		while ( aSubscription != Range.second )
	  {
			std::string
			Topic( PublishingLink::SetTopic( ActorName, EndpointID,
																		   aSubscription->second.name() ) );

			Subscription.subscribe( Topic );

			ActiveSubscriptions.emplace( Topic, ResolvedAddress.GlobalAddress,
																	 ActorName );

			PendingRequests.erase( aSubscription++ );
		}
	}
}

// When messages starts arriving on the subscription socket, the handler will
// be called. It will read the message off the socket and the first part of
// the message will be the subscribed topic, and the second part the message
// payload (content)
//
// It should be noted that it is not necessary to pass this message through
// the session layer server or the presentation layer since it is known that
// it is a serialised payload, and that the subscriber actor is the recipient.
// Consequently, the payload can be directly sent to this actor (send to self).

void Theron::ZeroMQ::Subscriber::SubscriptionHandler( void )
{
	zmqpp::message         TheMessage;
	std::string            Topic;

	Subscription.receive( TheMessage );

	TheMessage >> Topic;

	auto TheSubscription = ActiveSubscriptions.find( Topic );

	if ( TheSubscription != ActiveSubscriptions.end() )
  {
		SerialMessage::Payload Payload;

		TheMessage >> Payload;

		// Sending the received payload back to ourself, impersonating the sender
		// as the actor publishing this topic. This will invoke the handler for
		// serialised messages, which will produce the binary message that will
		// finally be delivered to the handler for the binary message type.

		Send( Payload,
					TheSubscription->second.Connection.GetActorAddress(), GetAddress() );
	}
}

// -----------------------------------------------------------------------------
// Constructor and destructor
// -----------------------------------------------------------------------------
//

Theron::ZeroMQ::Subscriber::Subscriber( const std::string & name )
: Actor( name ), StandardFallbackHandler( GetAddress().AsString() ),
  DeserializingActor( GetAddress().AsString() ),
  Subscription( PublishingLink::GetContext(), zmqpp::socket_type::subscribe ),
  ActiveSubscriptions(), PendingRequests()
{
	PublishingLink::GetMonitor().add( Subscription,
															 [this](void)->void{ SubscriptionHandler(); } );

	RegisterHandler( this, &Subscriber::ConnectSubscription );
}

// The destructor unsubscribes from all active subscriptions, and disconnect
// from all active connections before removing the socket from the socket
// monitor.

Theron::ZeroMQ::Subscriber::~Subscriber( void )
{
	// There can be many topics on the same connection, and so a set is used to
	// ensure that the recorded connection endpoints are all unique.

	std::set< std::string > ActiveConnections;

	// The subscribed topics are unsubscribed one by one. Note that there is
	// no need to remove them from the map of active subscriptions since the
	// everything will be deleted by the destructor of the active subscriptions
	// map.

	for ( auto aSubcription = ActiveSubscriptions.begin();
			       aSubcription != ActiveSubscriptions.end(); ++aSubcription )
  {
		Subscription.unsubscribe( aSubcription->first );
		ActiveConnections.insert( aSubcription->second.Connection.GetEndpoint() );
	}

	// Then the recorded connections can be disconnected one by one.

	for ( const std::string & aConnection : ActiveConnections )
		Subscription.disconnect( aConnection );

	// Finally, the monitoring of the closing socket should be stopped.

	PublishingLink::GetMonitor().remove( Subscription );
}
