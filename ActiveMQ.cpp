/*==============================================================================
Active Message Queue session layer

Please see the details in the associated header file.

Author and Copyright: Geir Horn, 2018
License: LGPL 3.0
==============================================================================*/

#include <vector>

#include "ActiveMQ.hpp"

#include <cms/TextMessage.h>
#include <cms/ObjectMessage.h>
#include <cms/MapMessage.h>
#include <cms/BytesMessage.h>

/*==============================================================================

 Messages

==============================================================================*/
//
// Dealing with the address conversion is trivial in this case where the 
// sender address simply corresponds to the actor address.

Theron::Address Theron::ActiveMQ::Message::ActorAddress ( 
  const Theron::ActiveMQ::GlobalAddress & ExternalActor ) const
{
	return ExternalActor.ActorAddress();
}

// The properties of the message can be transferred to the CMS message before 
// it is transmitted. To avoid double storage of the strings identifying the 
// sender actor and the receiving actor, they are treated separately.

void 
Theron::ActiveMQ::Message::StoreProperties( cms::Message * TheMessage ) const
{
	for ( auto & TheProperty : Properties )
		TheProperty.second->StoreProperty( TheProperty.first, TheMessage );
}

// Getting properties from a CMS message is basically testing repeatedly for 
// the type of the property and then store it locally. 

void Theron::ActiveMQ::Message::GetProperties( const cms::Message * TheMessage )
{
	std::vector< std::string > Labels( TheMessage->getPropertyNames() );
	
	// The properties are set according to the type identified for the property 
	// in the CMS message.
	
	for ( std::string & Label : Labels )
		switch ( TheMessage->getPropertyValueType( Label ) )
		{
			case cms::Message::BOOLEAN_TYPE:
				SetProperty( Label, TheMessage->getBooleanProperty( Label ) );
				break;
			case cms::Message::BYTE_TYPE:
				SetProperty( Label, TheMessage->getByteProperty( Label ) );
				break;
			case cms::Message::CHAR_TYPE:
				SetProperty( Label, TheMessage->getByteProperty( Label ) );
				break;
			case cms::Message::SHORT_TYPE:
				SetProperty( Label, TheMessage->getShortProperty( Label ) );
				break;
			case cms::Message::INTEGER_TYPE:
				SetProperty( Label, TheMessage->getIntProperty( Label ) );
				break;
			case cms::Message::LONG_TYPE:
				SetProperty( Label, TheMessage->getLongProperty( Label ) );
				break;
			case cms::Message::DOUBLE_TYPE:
				SetProperty( Label, TheMessage->getDoubleProperty( Label ) );
				break;
			case cms::Message::FLOAT_TYPE:
				SetProperty( Label, TheMessage->getFloatProperty( Label ) );
				break;
			case cms::Message::STRING_TYPE:
				SetProperty( Label, TheMessage->getStringProperty( Label ) );
				break;
			default:
		  {
				std::ostringstream ErrorMessage;
				
				ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									   << "Property type numeration "
										 << TheMessage->getPropertyValueType( Label )
										 << " is unknown and unhanded";
										 
			  throw std::logic_error( ErrorMessage.str() );
			}
		}
}

// The constructor for text messages based on a CMS text message pointer will 
// decode the message and throw an invalid argument exception if the conversion
// fails.

Theron::ActiveMQ::TextMessage::TextMessage(
	const cms::TextMessage * TheTextMessage )
: Message( 
		TheTextMessage != nullptr ? 
						GlobalAddress( TheTextMessage->getStringProperty("SendingActor") )
						: GlobalAddress( "", "" ),
		TheTextMessage != nullptr ?
				    GlobalAddress( TheTextMessage->getStringProperty("ReceivingActor") )
						: GlobalAddress( "", "" ),
		TheTextMessage != nullptr ?
						TheTextMessage->getText() : "",
		Type::TextMessage,
		TheTextMessage != nullptr ?
					  Message::GetDestination( TheTextMessage ) : Destination::Topic )
{
	if ( TheTextMessage != nullptr )
		GetProperties( TheTextMessage );
	else
  {
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
		             << "A text message could not be constructed from a pointer "
								 << "not to a text message ";
								 
	  throw std::invalid_argument( ErrorMessage.str() );
	}
}


/*==============================================================================

 Network layer

==============================================================================*/
//

void Theron::ActiveMQ::NetworkLayer::CreateDestination(
	const std::string & TopicOrQueue, ActiveMQ::Message::Destination Type)
{ 
	switch( Type )
	{
		case ActiveMQ::Message::Destination::Topic :
			Destinations.emplace( TopicOrQueue, 
			 						          AMQSession->createTopic( TopicOrQueue ));
			break;
		case ActiveMQ::Message::Destination::Queue :
			Destinations.emplace( TopicOrQueue, 
									          AMQSession->createQueue( TopicOrQueue ));
			break;
	}
}

// -----------------------------------------------------------------------------
// AMQ Messages
// -----------------------------------------------------------------------------
//
// Messages arriving from the AMQ server (broker) will be converted to a text 
// message and sent to the session layer. In the case the message is not a 
// text message, a logic error exception will be thrown.

void Theron::ActiveMQ::NetworkLayer::InboundMessage( 
 const cms::Message * TheMessage )
{
	try
	{
	  Send( Theron::ActiveMQ::TextMessage( TheMessage ),
				  Theron::Network::GetAddress( Theron::Network::Layer::Session )	);
	}
	catch ( std::invalid_argument & Error )
  {
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": ";
		
		if ( dynamic_cast< const cms::TextMessage * >( TheMessage ) != nullptr )
			ErrorMessage << "Text message error: " << Error.what();
		else
		{
	    ErrorMessage << "Theron++ Active MQ only supports text messages, but ";
    
		  if ( dynamic_cast< const cms::StreamMessage * >( TheMessage ) != nullptr )
				ErrorMessage << " a Stream Message ";
			else 
			if ( dynamic_cast< const cms::ObjectMessage * >( TheMessage ) != nullptr )
				ErrorMessage << " an Object Message ";
			else
			if ( dynamic_cast< const cms::MapMessage * >( TheMessage ) != nullptr )
				ErrorMessage << " a Map Message ";
			else
			if ( dynamic_cast< const cms::BytesMessage * >( TheMessage ) != nullptr )
				ErrorMessage << " a Byte Message ";
			
			ErrorMessage << "was received instead.";
		}
			
		throw std::runtime_error( ErrorMessage.str() );
	}
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
	
	// If there is a destination object for this topic or queue it is readily 
	// reused to send the message. If not the message will be sent from a 
	// temporary destination. 

	auto Destination = Destinations.find( TopicOrQueue );
		
	if ( Destination != Destinations.end() )
		Producer->send( Destination->second.get(), Outbound );
	else
  {
		cms::Destination * TemporaryDestination;
		
		switch ( TheMessage.DestinationMode )
		{
			case ActiveMQ::Message::Destination::Topic :
				TemporaryDestination = AMQSession->createTopic( TopicOrQueue );
				break;
			case ActiveMQ::Message::Destination::Queue :
				TemporaryDestination = AMQSession->createQueue( TopicOrQueue );
				break;
		}
		
		Producer->send( TemporaryDestination, Outbound );
		delete TemporaryDestination;
	}
	
	// Cleaning up the message object
	
	delete Outbound;
}

// The command handler receives requests from remote endpoints. The current 
// set of commands is only the actor discovery, where a discovery request is 
// sent to the discovery topic, and where the endpoint hosting that actor will 
// return a properly formatted response.
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
				 
				 if ( ( !ActorName.empty() ) && IsLocalActor( ActorName ) )
				 {
					 auto Response = AMQSession->createTextMessage();
					 
					 Response->setShortProperty( "Command", 
						   static_cast< short int >( Command::DiscoveryResponse ));
					 Response->setStringProperty( "ActorName", ActorName );
					 Response->setStringProperty( "GlobalAddress", 
					     GlobalAddress( ActorName, GetAddress().AsString() ).AsString() );
					 
					 Producer->send( Destinations[ DiscoveryTopic ].get(), Response );
					 
					 delete Response;
				 }
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

// The message handler that receives a request to find an actor on one of the 
// other endpoints will create an AMQ text message and send this on the 
// discovery channel.

void Theron::ActiveMQ::NetworkLayer::ResolveAddress(
	const Theron::NetworkLayer< TextMessage >::ResolutionRequest & TheRequest, 
	const Theron::Actor::Address TheSessionLayer)
{
	auto TheMessage = AMQSession->createTextMessage();
	
	if ( TheRequest.NewActor.IsLocalActor() )
		TheMessage->setShortProperty( "Command", 
											 static_cast< short int >( Command::DiscoveryResponse ) );
	else
		TheMessage->setShortProperty( "Command", 
											 static_cast< short int >( Command::DiscoveryRequest ) );
	
	TheMessage->setStringProperty( "ActorName", TheRequest.NewActor.AsString() );
	TheMessage->setStringProperty( "GlobalAddress", 
		GlobalAddress( TheRequest.NewActor.AsString(), 
									 GetAddress().AsString() ).AsString() );
	
	Producer->send( Destinations[ DiscoveryTopic ].get(), TheMessage );
	
	delete TheMessage;
}

// The remove actor function will send a message on the discovery channel that 
// the given actor has been removed. 

void Theron::ActiveMQ::NetworkLayer::ActorRemoval(
	const Theron::NetworkLayer< TextMessage >::RemoveActor & TheCommand, 
	const Theron::Actor::Address TheSessionLayer)
{
	auto TheMessage = AMQSession->createTextMessage();
	
	TheMessage->setShortProperty( "Command", 
														 static_cast< short int >( Command::ActorRemoved ));
	TheMessage->setStringProperty( "GlobalAddress", 
															TheCommand.GlobalAddress.AsString() );
	
	Producer->send( Destinations[ DiscoveryTopic ].get(), TheMessage );
	
	delete TheMessage;
}

// -----------------------------------------------------------------------------
// Handling subscriptions
// -----------------------------------------------------------------------------
//
// When a subscription is requested the corresponding destination is created 
// and a subscription is registered with the inbound message handler. As the 
// message is not generated by a remote actor, the sending actor and the 
// destination actor fields of the inbound text message may be empty and they 
// must be completed by the session layer.

void Theron::ActiveMQ::NetworkLayer::NewSubscription(
	const Theron::ActiveMQ::NetworkLayer::CreateSubscription & Request, 
	const Theron::Actor::Address From )
{
	CreateDestination( Request.TopicOrQueue, Request.ChannelType );
	
	Subscriptions.emplace( Request.TopicOrQueue, 
		new MessageMonitor( AMQSession, Destinations[ Request.TopicOrQueue ],
												[this]( const cms::Message * TheMessage )->void{ 
													InboundMessage( TheMessage ); }	)	);
}

// The removal of a subscription consists of finding the corresponding 
// subscription and delete it. Then the destination is deleted after the 
// subscription since it was used to create the subscription. Finally the 
// removal is confirmed back to the session layer (the sender of the request)
// for it to clean up all bindings this subscription may have to local actors. 

void Theron::ActiveMQ::NetworkLayer::RemoveSubscription(
	const Theron::ActiveMQ::NetworkLayer::CancelSubscription & Request, 
	const Theron::Actor::Address From)
{
	auto TheSubscription = Subscriptions.find( Request.TopicOrQueue );
	
	if ( TheSubscription != Subscriptions.end() )
		Subscriptions.erase( TheSubscription );
	
	auto TheDestination = Destinations.find( Request.TopicOrQueue );
	
	if ( TheDestination != Destinations.end() )
		Destinations.erase( TheDestination );
	
	Send( Request, From );
}
 
// -----------------------------------------------------------------------------
// Constructor and destructor
// -----------------------------------------------------------------------------
//

Theron::ActiveMQ::NetworkLayer::NetworkLayer( 
	const std::string & EndpointName, const std::string & AMQServerIP, 
	const std::string & Port)
: Actor( EndpointName ), StandardFallbackHandler( EndpointName ),
  Theron::NetworkLayer< TextMessage >( EndpointName ),
  AMQConnection( nullptr ), AMQSession( nullptr ),
  Destinations(), 
  ProducerResource( nullptr ), Producer( nullptr ), 
  Subscriptions()
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
	
	CreateDestination( DiscoveryTopic, ActiveMQ::Message::Destination::Topic );
	CreateDestination( EndpointName,   ActiveMQ::Message::Destination::Topic );
	
  Subscriptions.emplace( DiscoveryTopic, 
						    new MessageMonitor( AMQSession, Destinations[ DiscoveryTopic ], 
								      [this]( const cms::Message * TheMessage )->void{ 
												 CommandHandler( TheMessage ); } ) );
	
	Subscriptions.emplace( EndpointName, 
							  new MessageMonitor( AMQSession, Destinations[ EndpointName ], 
											[this]( const cms::Message * TheMessage )->void{ 
												 InboundMessage( TheMessage ); } ) ); 
	
	// Then create the producer resource and use it to initialise the actual 
	// producer. Persistent message delivery is configured since there is no way 
	// to detect lost messages unless the application puts in place a higher 
	// level protocol. The producer resource is created for the discovery topic 
	// although it will be used by the producer for other destinations later.
	
	ProducerResource = AMQSession->createProducer( 
																				Destinations[ DiscoveryTopic ].get() );
	
	Producer = new activemq::cmsutil::CachedProducer( ProducerResource );

	Producer->setDeliveryMode( cms::DeliveryMode::PERSISTENT );
	
	// Registering message handlers defined in this class (not the overridden 
	// handlers as they are already registered by the base class)
	
	RegisterHandler( this, &NetworkLayer::NewSubscription    );
	RegisterHandler( this, &NetworkLayer::RemoveSubscription );
}

Theron::ActiveMQ::NetworkLayer::~NetworkLayer()
{
	AMQSession->stop();
	AMQConnection->stop();
	AMQConnection->close();
	activemq::library::ActiveMQCPP::shutdownLibrary();
	delete Producer;
	delete ProducerResource;
	delete AMQSession;
	delete AMQConnection;
}
