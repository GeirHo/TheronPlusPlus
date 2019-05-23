/*==============================================================================
Active Message Queue session layer

Please see the details in the associated header file.

Author and Copyright: Geir Horn, 2018-2019
License: LGPL 3.0
==============================================================================*/

// Standard headers

#include <vector>   // Standard vectors
#include <utility>  // For pairs

// AMQ headers

#include <cms/TextMessage.h>
#include <cms/ObjectMessage.h>
#include <cms/MapMessage.h>
#include <cms/BytesMessage.h>

// Theron++ related headers

#include "Communication/NetworkLayer.hpp"
#include "Communication/NetworkEndpoint.hpp"

#include "Communication/AMQ/AMQNetworkLayer.hpp"

/*==============================================================================

 Topic and queue management

==============================================================================*/
//
//
// A destination pointer is created based on the managed session when a
// for a topic or queue name.

Theron::ActiveMQ::NetworkLayer::DestinationPointer
Theron::ActiveMQ::NetworkLayer::CreateDestination(
	const std::string & TopicOrQueue, ActiveMQ::Message::Destination Type)
{
	DestinationPointer Destination;

	switch( Type )
	{
		case ActiveMQ::Message::Destination::Topic :
			Destination = DestinationPointer(AMQSession->createTopic( TopicOrQueue ));
			break;
		case ActiveMQ::Message::Destination::Queue :
			Destination = DestinationPointer(AMQSession->createQueue( TopicOrQueue ));
			break;
	}

	return Destination;
}

// A producer is created on a destination and can be used to send messages only
// to that destination. It takes the same arguments as the above function to
// create the destination, but uses this as an argument for the session to
// create the producer object and insert this into the map of producers.

void Theron::ActiveMQ::NetworkLayer::CreateProducer(
	const std::string & TopicOrQueue, ActiveMQ::Message::Destination Type )
{
	if ( Producers.find( TopicOrQueue ) == Producers.end() )
  {
		// Create a new destination

		DestinationPointer NewDestination = CreateDestination( TopicOrQueue, Type );

		// The new producer is created and inserted in the producer database. Even
		// though the lookup is inexpensive on an unordered map, producing the hash
		// value for the topic or queue is not for free, and therefore it is more
		// efficient to store the iterator returned by the emplace function.

	  auto NewProducer =
			   Producers.emplace( TopicOrQueue,
						new activemq::cmsutil::CachedProducer(
								AMQSession->createProducer( NewDestination.get() ) ) );

		// The first element of the new producer is an iterator to the inserted
		// producer record and it is used to set the delivery modus to persistent.
	  // Persistent message delivery is configured since there is no way
		// to detect lost messages unless the application puts in place a higher
		// level protocol.

		NewProducer.first->second->setDeliveryMode( cms::DeliveryMode::PERSISTENT );
	}
}

// The command handler receives requests from remote endpoints. The current
// set of commands is only the actor discovery, where a discovery request is
// sent to the discovery topic, and where the endpoint hosting that actor will
// return a properly formatted response. Note that the requested actor must
// exist in the distributed actor system at the time of the request as there
// is no caching of requests. If a derived implementation should require that
// dynamically created actors can be requested before they exists, then the
// requester must periodically resend the request. This is the responsibility
// of the requester.
//
// When the response is received, it is forwarded as a resolution response to
// the session layer that will store the mapping of the external address of
// each actor

void Theron::ActiveMQ::NetworkLayer::CommandHandler(
	const cms::Message * TheMessage )
{
	const cms::TextMessage * TheTextMessage
										  = dynamic_cast< const cms::TextMessage * >( TheMessage );

	if ( TheTextMessage != nullptr )
  {
    switch ( static_cast< Command >(
								TheTextMessage->getShortProperty( "Command" ) ) )
		{
			case Command::DiscoveryRequest :
			 {
				 std::string ActorName
														 = TheTextMessage->getStringProperty( "ActorName" );

				 if ( !ActorName.empty() )
					 Send( ResolutionRequest( Address( ActorName ) ),
								 Theron::Network::GetAddress(Theron::Network::Layer::Session) );
			 }
			 break;
			case Command::DiscoveryResponse :
			 {
				 ResolutionResponse ForTheRecord(
				  GlobalAddress( TheTextMessage->getStringProperty( "GlobalAddress" ) ),
					Address( TheTextMessage->getStringProperty( "ActorName" ) ) );

				 Send( ForTheRecord,
							 Theron::Network::GetAddress( Theron::Network::Layer::Session ) );
			 }
			 break;
			case Command::ActorRemoved :
			 {
				 RemoveActor TheMessage(
				 GlobalAddress( TheTextMessage->getStringProperty("GlobalAddress") ) );

				 Send( TheMessage,
							 Theron::Network::GetAddress( Theron::Network::Layer::Session ) );
			}
			case Command::EndpointShutDown :
		  {
				// Given that endpoints are tracked only to the extent that some actors
				// on this endpoint sends to remote endpoints, and that the endpoint
				// should have sent remove actor messages for all hosted actors,
				// there is not more to do than delete the producer for the closing
				// endpoint at this stage.

				auto ClosingEndpoint =
	      Producers.find( TheTextMessage->getStringProperty( "Endpoint" ) );

				if ( ClosingEndpoint != Producers.end() )
					Producers.erase( ClosingEndpoint );
			}
		}
	}
	else
  {
		std::ostringstream ErrorMessage;

		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << "AMQ Network command handler received a message that is "
								 << "not a text message. Only text messages are supported.";

	  throw std::invalid_argument( ErrorMessage.str() );
	}
}

// A discovery response is sent if the session layer knows the external address
// of a resolution request. The session layer will then send a resolution
// response back to the network layer, and a discovery response message will
// be published on the discovery channel.

void Theron::ActiveMQ::NetworkLayer::ResolvedAddress(
	const Theron::NetworkLayer<TextMessage>::ResolutionResponse & TheResponse,
	const Theron::Actor::Address TheSessionLayer )
{
	 auto Response = AMQSession->createTextMessage();

	 Response->setShortProperty( "Command",
		         static_cast< short int >( Command::DiscoveryResponse ));
	 Response->setStringProperty( "ActorName", TheResponse.TheActor.AsString() );
	 Response->setStringProperty( "GlobalAddress",
																TheResponse.GlobalAddress.AsString() );

	 // The discovery topic should exist and the package is forwarded.

	 Producers[ DiscoveryTopic ]->send( Response );

	 delete Response;
}


/*==============================================================================

 Data messages

==============================================================================*/
//
// Messages arriving from the AMQ server (broker) will be converted to a text
// message and sent to the session layer. In the case the message is not a
// text message, a logic error exception will be thrown.

void Theron::ActiveMQ::NetworkLayer::InboundMessage(
 const cms::Message * TheMessage )
{
  Send( Theron::ActiveMQ::TextMessage( TheMessage ),
			  Theron::Network::GetAddress( Theron::Network::Layer::Session )	);
}

// Sending messages converts from the text message to the CMS text message
// and publishes this on the queue indicated as the destination. It first
// creates the destination and a producer for this destination, before it
// creates the message to send. It should be noted that a temporary
// destination is created and used if there is no existing destination for
// the given topic.

void Theron::ActiveMQ::NetworkLayer::OutboundMessage(
 const Theron::ActiveMQ::TextMessage & TheMessage,
 const Theron::Actor::Address From )
{
	// Obtaining the destination identifier and find the destination for this
	// topic if it is in the list of cached destinations.

	std::string TopicOrQueue = TheMessage.GetRecipient().Endpoint();

	// Creating the text message to send

	cms::TextMessage * Outbound =
								     AMQSession->createTextMessage( TheMessage.GetPayload() );

  Outbound->setStringProperty( "SendingActor",
															 TheMessage.GetSender().AsString() );
	Outbound->setStringProperty( "ReceivingActor",
															 TheMessage.GetRecipient().AsString() );
	Outbound->setStringProperty( "AMQDestination", TopicOrQueue );

	TheMessage.StoreProperties( Outbound );

	// If there is no producer object for the in-box of the remote endpoint
	// it will be created since we know that there is at least one actor on
	// this endpoint that would like to communicate with actors on that
	// endpoint.

	if ( Producers.find( TopicOrQueue ) == Producers.end() )
		CreateProducer( TopicOrQueue, TheMessage.DestinationMode );

	// Then the message can be sent to the destination.

	Producers[ TopicOrQueue ]->send( Outbound );

	// Cleaning up the message object

	delete Outbound;
}

/*==============================================================================

 Session layer interaction

==============================================================================*/
//
// -----------------------------------------------------------------------------
// Mapping local actor addresses to global addresses
// -----------------------------------------------------------------------------
//
// A resolution request is sent from the session layer to find out the external
// address of an actor. There are two situations to consider: The actor is a
// local actor and the session layer just needs to know its external address
// to set the right sender for a message; or it is a remote actor for which
// a discovery request must be sent on the discovery topic to all the other
// peer AMQ network layers. The response to the session layer will then be
// created only when the response from one of the other network endpoints
// comes back.

void Theron::ActiveMQ::NetworkLayer::ResolveAddress(
	const Theron::NetworkLayer< TextMessage >::ResolutionRequest & TheRequest,
	const Theron::Actor::Address TheSessionLayer)
{

	if ( TheRequest.NewActor.IsLocalActor() )
  {
		// Construct the response with the global address containing this endpoint's
		// address

		ResolutionResponse
		TheResponse( GlobalAddress( TheRequest.NewActor, GetAddress().AsString() ),
								 TheRequest.NewActor );

		Send( TheResponse, TheSessionLayer );

		// Then send a discovery response message to all the other endpoints that
		// this actor has become available. This is done by generating a resolution
		// response message to this network layer 'self', a message that will be
		// handled by the resolved address handler that generates the message on
		// the discovery topic.

		Send( TheResponse, GetAddress() );
	}
	else
  {
		auto TheMessage = AMQSession->createTextMessage();

		TheMessage->setShortProperty( "Command",
											 static_cast< short int >( Command::DiscoveryRequest ) );

		TheMessage->setStringProperty( "ActorName",
																	 TheRequest.NewActor.AsString() );

		// Then posting this discovery request on the discovery topic channel.

		Producers[ DiscoveryTopic ]->send( TheMessage );

		delete TheMessage;
	}
}

// The remove actor function will send a message on the discovery channel that
// the given actor has been removed. The remote command handler will forward
// this request to its session layer which should remove the external address
// mapping for this the removed actor if the global address for that actor
// is cached.

void Theron::ActiveMQ::NetworkLayer::ActorRemoval(
	const Theron::NetworkLayer< TextMessage >::RemoveActor & TheCommand,
	const Theron::Actor::Address TheSessionLayer)
{
	auto TheMessage = AMQSession->createTextMessage();

	TheMessage->setShortProperty( "Command",
						  static_cast< short int >( Command::ActorRemoved ));

	TheMessage->setStringProperty( "GlobalAddress",
														     TheCommand.GlobalAddress.AsString() );

	Producers[ DiscoveryTopic ]->send( TheMessage );

	delete TheMessage;
}

// -----------------------------------------------------------------------------
// Handling subscriptions
// -----------------------------------------------------------------------------
//
// When a subscription is requested the corresponding message monitor is created
// and a subscription is registered with the inbound message handler. As the
// message is not generated by a remote actor, the sending actor and the
// destination actor fields of the inbound text message may be empty and they
// must be completed by the session layer. A special handler is used to ensure
// this completion by storing the topic or queue identifier as the first
// argument to the handler, see handler below.

void Theron::ActiveMQ::NetworkLayer::NewSubscription(
	const Theron::ActiveMQ::NetworkLayer::CreateSubscription & Request,
	const Theron::Actor::Address From )
{
	std::string TopicOrQueue( Request.TopicOrQueue );

	Subscriptions.emplace( TopicOrQueue,
	  std::make_unique< MessageMonitor >(
			AMQSession, CreateDestination( TopicOrQueue, Request.ChannelType ),
				[this,TopicOrQueue]( const cms::Message * TheMessage )->void{
		      SubscriptionHandler( TopicOrQueue, TheMessage ); }	)	);
}

// The removal of a subscription consists of finding the corresponding
// subscription and delete it. Then the removal is confirmed back to the
// session layer (the sender of the request) for it to clean up all bindings
// this subscription may have to local actors.

void Theron::ActiveMQ::NetworkLayer::RemoveSubscription(
	const Theron::ActiveMQ::NetworkLayer::CancelSubscription & Request,
	const Theron::Actor::Address From)
{
	auto TheSubscription = Subscriptions.find( Request.TopicOrQueue );

	if ( TheSubscription != Subscriptions.end() )
		Subscriptions.erase( TheSubscription );

	Send( Request, From );
}

// The subscription handler sets up the text message to the session layer which
// will further distribute the message to subscribing actors. It will set the
// sender's actor address to NULL to indicate that no actor should respond back
// to a message received from this channel.

void Theron::ActiveMQ::NetworkLayer::SubscriptionHandler(
	const std::string TopicOrQueue, const cms::Message * TheMessage )
{
	GlobalAddress Sender( TopicOrQueue, "" ), Receiver( "" , "" );

	const cms::TextMessage *
	ReceivedMessage( TextMessage::Validate( TheMessage ) );

	Send( TextMessage( Sender, Receiver, ReceivedMessage->getText() ),
				Theron::Network::GetAddress(Theron::Network::Layer::Session)  );
}

/*==============================================================================

 Shut down management

==============================================================================*/
//
// The shut down message received from the session layer when all local actors
// have de-registered in response to a shut down request on the node endpoint
// will send a shut down message on the discovery topic and then close the
// connection and the library.

void Theron::ActiveMQ::NetworkLayer::Stop(
	const Network::ShutDown & StopMessage, const Theron::Actor::Address Sender )
{

	auto TheMessage = AMQSession->createTextMessage();

	TheMessage->setShortProperty( "Command",
										 static_cast< short int >( Command::EndpointShutDown ) );

	TheMessage->setStringProperty( "Endpoint", GetAddress().AsString() );
	Producers[ DiscoveryTopic ]->send( TheMessage );

	delete TheMessage;

	// Then stop the connections with the AMQ server

	AMQSession->stop();
	AMQConnection->stop();
	AMQConnection->close();
}


/*==============================================================================

 Constructor and destructor

==============================================================================*/
//
// The name of the network layer actor is the endpoint name given and it is
// used for constructing the global addresses of the actors on this endpoint
// as well as the name for the inbound mailbox. Hence this name should be
// unique at the system level. However the way the global address is constructed
// allows actors on different endpoints to have the same name as the endpoint
// name is a part of their global address.
//
// The constructor then initialises the AMQ library and the connection factory
// which is only used to connect to the broker and starting the session with
// this broker. Then two destinations will be created: One for the discovery
// topic used for all endpoints, and one for the inbound mailbox for this
// endpoint. Since the network layer only will publish to the discovery topic,
// it is sufficient with a single producer. It should be noted that this
// producer is reused when sending data messages as it supports sending to
// arbitrary destinations.

Theron::ActiveMQ::NetworkLayer::NetworkLayer(
	const std::string & EndpointName, const std::string & AMQServerIP,
	const std::string & Port)
: Actor( EndpointName ), StandardFallbackHandler( EndpointName ),
  Theron::NetworkLayer< TextMessage >( EndpointName ),
  AMQConnection( nullptr ), AMQSession( nullptr ),
  Producers(), Subscriptions()
{
	// Starting the AMQ interface and creating the connection using a temporary
	// connection factory (who let the Java programmers in?)

	activemq::library::ActiveMQCPP::initializeLibrary();

	activemq::core::ActiveMQConnectionFactory * AMQFactory
														= new activemq::core::ActiveMQConnectionFactory();

	AMQFactory->setBrokerURI( "tcp://" + AMQServerIP + ":" + Port );

	AMQConnection = dynamic_cast< activemq::core::ActiveMQConnection *>(
																AMQFactory->createConnection()  );

	delete AMQFactory;

	// Creating the session and starting the connection and the session

	AMQSession = dynamic_cast< activemq::core::ActiveMQSession * >(
		AMQConnection->createSession( cms::Session::AUTO_ACKNOWLEDGE ) );

	AMQConnection->start();
	AMQSession->start();

	// Subscribe to the two default destinations: The global topic for discovery
	// and the in-box of this endpoint.

	Subscriptions.emplace( DiscoveryTopic,
	  std::make_unique< MessageMonitor >( AMQSession,
			CreateDestination( DiscoveryTopic, ActiveMQ::Message::Destination::Topic ),
			[this]( const cms::Message * TheMessage )->void{
			 			  CommandHandler( TheMessage ); } ) );

	Subscriptions.emplace( EndpointName,
	  std::make_unique< MessageMonitor >( AMQSession,
			CreateDestination( EndpointName, ActiveMQ::Message::Destination::Topic ),
			[this]( const cms::Message * TheMessage )->void{
			 			  InboundMessage( TheMessage ); } ) );

	// Then create the producer resource for the discovery topic

  CreateProducer( DiscoveryTopic, ActiveMQ::Message::Destination::Topic );

	// Registering message handlers defined in this class (not the overridden
	// handlers as they are already registered by the base class)

	RegisterHandler( this, &NetworkLayer::NewSubscription    );
	RegisterHandler( this, &NetworkLayer::RemoveSubscription );
}

// The destructor simply reverses the constructor by stopping the session
// and connection and then closing the connection and deleting the AMQ objects
// before the AMQ library is stopped.

Theron::ActiveMQ::NetworkLayer::~NetworkLayer()
{
	// If the actor is closing directly and not as a result of a shut down
	// request, the connection is still running and the shut down request
	// handler will be called explicitly to inform the other endpoints and
	// close the connection.

	if ( ! AMQConnection->isClosed() )
		Stop( Network::ShutDown(), Theron::Address::Null() );

	// Clean up the local classes dynamically allocated

	Producers.clear();
	Subscriptions.clear();

	delete AMQSession;
	delete AMQConnection;

	activemq::library::ActiveMQCPP::shutdownLibrary();
}
