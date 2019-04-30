/*==============================================================================
Active Message Queue session layer

Please see the details in the associated header file.

Author and Copyright: Geir Horn, 2017
License: LGPL 3.0
==============================================================================*/

#include <vector>    // Standard vector to hold property labels

#include "AMQSessionLayer.hpp"

/*==============================================================================

 Static variables

==============================================================================*/
//
// The flag to prevent multiple initialisations of the ActiveMQ library

bool Theron::ActiveMQ::SessionLayer::AMQInitialiser::Uninitialised = true;

// There are also two static pointer for the connection management

std::shared_ptr< cms::ConnectionFactory > 
	Theron::ActiveMQ::SessionLayer::Factory;
	
std::shared_ptr< cms::Connection > 
	Theron::ActiveMQ::SessionLayer::Connection;

/*==============================================================================

 Messages

==============================================================================*/
//
// Only the constructor from a CMS message needs to be defined for the base 
// class message type

Theron::ActiveMQ::Message::Message( 
	const cms::Message * TheMessage, 
	const Type MessageClass,
	const Session Transmission )
: Properties(),
  DestinationMode( GetDestination( TheMessage ) ),
  SessionType( Transmission ), MessageType( MessageClass ),
  DestinationID( GetDestinationID( TheMessage ) )
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


	
/*==============================================================================

 Session Layer

==============================================================================*/
//
// -----------------------------------------------------------------------------
// Transaction
// -----------------------------------------------------------------------------
//
// The constructor creates the producer and the consumer if there is a session 
// for this topic or queue. If the session layer is not connected they are 
// left uninitialised making messages sent on this transaction to be forwarded 
// to the local subscribers only.

Theron::ActiveMQ::SessionLayer::Transaction::Transaction(
	const Theron::ActiveMQ::Message::Destination Type, 
	const std::string & QueueOrTopicName, 
	const std::shared_ptr< cms::Session > & Session, 
	Theron::ActiveMQ::SessionLayer * Host)
: Producer(), Consumer(), Subscribers(), ThisSessionLayer( Host )
{
	if ( Session )
  { 
		// The destination object corresponding to this transaction is 
		// created first as it is needed to create the producer and consumer
		
		std::shared_ptr< cms::Destination > Destination;
		
		switch ( Type )
		{
			case Theron::ActiveMQ::Message::Destination::Topic :
				Destination = std::make_shared< cms::Destination >(
					Session->createTopic( QueueOrTopicName ) );
				break;
			case Theron::ActiveMQ::Message::Destination::Queue : 
				Destination = std::make_shared< cms::Destination >(
					Session->createQueue( QueueOrTopicName ) );
				break;
		};
		
		// Then this destination can be used to create the producer and the 
		// consumer using the session.
		
		Producer = std::make_shared< cms::MessageProducer >( 
			Session->createProducer( Destination.get() ) );
		
		Consumer = std::make_shared< cms::MessageConsumer >( 
			Session->createConsumer( Destination.get() ) );
		
		// Messages sent from this client shall be marked as persistent to 
		// ensure that they will be received by the AMQ message broker. It 
		// should be noted that this does not guarantee that they are delivered
		// to any subscribing clients as it is only the communication to the 
		// AMQ message broker that is marked as persistent.
		
		Producer->setDeliveryMode( cms::DeliveryMode::PERSISTENT );
		
		// In the same way the message listener for the inbound messages for this
		// transaction is set to the transaction object itself.
		
		Consumer->setMessageListener( this );
	}
}

// The message handler for the commit transaction class is simply to make sure 
// that the message is properly acknowledged when it has been handled. Note 
// that there is no reason to test if the session is really transaction based 
// since this hander would not be used if it had not been transacted.

void Theron::ActiveMQ::SessionLayer::CommitTransaction::onMessage(
	const cms::Message * TheMessage )
{
	Transaction::onMessage( TheMessage );
	Session->commit();
}


// -----------------------------------------------------------------------------
// Session Manager
// -----------------------------------------------------------------------------
//
// Adding a subscription requires creating a new transaction if a transaction 
// for the given topic does not exist already.

void Theron::ActiveMQ::SessionLayer::SessionManager::AddSubscription(
	const std::string & TheQueueOrTopic, const Theron::Actor::Address & TheActor )
{
	std::shared_ptr< Transaction > TheTransaction;
	
	// The correct transaction is then looked up in the transaction map, and if 
	// it does not exist it will be created. 
	
	try
	{
		TheTransaction = Topics.at( TheQueueOrTopic );
	}
	catch ( std::out_of_range & Error )
	{
		// First the right transaction type is created depending on whether the 
		// session is transacted or not.
		
		if ( Session->isTransacted() )
			TheTransaction = std::make_shared< CommitTransaction >(
												Type, TheQueueOrTopic, Session, ThisSessionLayer);
		else
			TheTransaction = std::make_shared< Transaction >(
												Type, TheQueueOrTopic, Session, ThisSessionLayer);
		
		// Then the transaction is stored in the list of topics, and an exception 
		// is generated if this fails.
			
		auto Result = Topics.emplace( TheQueueOrTopic, TheTransaction );
		
		if ( ! Result.second )
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "Session manager failed to create transaction for ";
		  
		  if ( Type == Theron::ActiveMQ::Message::Destination::Queue )
				ErrorMessage << "queue ";
			else if ( Type == Theron::ActiveMQ::Message::Destination::Topic )
				ErrorMessage << "topic ";
			
			ErrorMessage << TheQueueOrTopic;
			
			throw std::runtime_error( ErrorMessage.str() );
		}
	}
	
	// The transaction should be valid at this point and the actor can be 
	// added as a subscriber
	
	TheTransaction->AddSubscription( TheActor );
}

// Removing a subscription is simpler as it is only to look it up, and 
// if the transaction exists, the actor's address is removed, and if this 
// was the last actor the whole topic record can be removed.

void Theron::ActiveMQ::SessionLayer::SessionManager::RemoveSubscription(
	const std::string& TheQueueOrTopic, const Theron::Actor::Address& TheActor)
{
	auto Record = Topics.find( TheQueueOrTopic );
	
	if ( Record != Topics.end() )
  {
		// The transaction record did exist, and the second field of the record is
		// the transaction object pointer that can be used to remove the 
		// subscription
		
		Record->second->RemoveSubscription( TheActor );
		
		// If this was the last subscription for this topic, the whole topic should 
		// be removed.
		
		if ( Record->second->NumberOfSubscribers() == 0 )
			Topics.erase( Record );
	}
}

// Sending a message is fundamentally only to look up the transaction for the 
// given topic, and if it exists it will be the "second" element of the lookup 
// returned record and its send function will take care of the message. 
// Otherwise, the producer will be set to a temporary producer for this 
// destination. It may seem wasteful to create the destination for each message 
// to send, but the issue is that there is no way to know when the last message
// has been sent to a destination, and consequently no way to know when the 
// destination can be removed. If a local actor knows that multiple messages 
// will be generated for a destination, it should subscribe to the destination 
// first because this will create the transaction object and significantly 
// reduce the cost of sending each message. Then when the transaction is no 
// longer needed, the actor should unsubscribe.

void Theron::ActiveMQ::SessionLayer::SessionManager::Send(
	const std::string & TheQueueOrTopic, 
	const std::shared_ptr<cms::Message> & TheMessage, 
	const bool CommitMessages )
{
	auto TransactionRecord = Topics.find( TheQueueOrTopic );
	
	if ( TransactionRecord != Topics.end() )
	  TransactionRecord->second->Send( TheMessage );
	else
  {
		std::shared_ptr< cms::Destination > Destination;
		
		switch( Type )
	  {
			case Theron::ActiveMQ::Message::Destination::Topic :
				Destination = std::make_shared< cms::Destination > (
												Session->createTopic( TheQueueOrTopic ) );
				break;
			case Theron::ActiveMQ::Message::Destination::Queue :
				Destination = std::make_shared< cms::Destination >(
												Session->createQueue( TheQueueOrTopic ) );
				break;
		}
		
		// The the producer can be created and used to send the message
		
		std::shared_ptr< cms::MessageProducer > 
		Producer( Session->createProducer( Destination.get() ) );
		
		Producer->send( TheMessage.get() );
	}
	
	// If the message is transacted and this is the last message of the 
	// sequence it is committed if the flag asks the manager to do so, and if 
	// the session is transacted.
	
	if ( CommitMessages && Session->isTransacted() )
		Session->commit();
}

// -----------------------------------------------------------------------------
// Topic or queue subscriptions
// -----------------------------------------------------------------------------
//
// The subscription handler will first retrieve an existing session manager 
// for the type of session the channel belongs to, and consider it an exception 
// if this does not exist for some reason. With the session manager in place,
// the subscription for the topic can be added.

void Theron::ActiveMQ::SessionLayer::AddSubscription(
	const Theron::ActiveMQ::SessionLayer::Subscription & TheSubscription, 
	const Theron::Address TheSubscriber)
{
	// Find first the right map of managers based on the type.
	
	auto DestinationMap = Session.find( TheSubscription.Type );
	
	if ( DestinationMap != Session.end() )
  {
		// The type of the destination exists, and it is necessary to check if the 
		// manager exists because the destination type is known.
		
		auto TheManager = 
			DestinationMap->second.find( TheSubscription.DestinationType );
			
		if ( TheManager == DestinationMap->second.end() )
		{
			// The destination type did not exists, and so it has to be created in 
			// the second level map. 
			
			auto Result = DestinationMap->second.emplace( 
										TheSubscription.DestinationType, 
										TheSubscription.QueueOrTopic, 
										TheSubscription.Type, Connection, this );
			
			if( Result.second )
		  {
				TheManager = Result.first;
				TheManager->second.AddSubscription( TheSubscription.QueueOrTopic, 
																						TheSubscriber );
			}
			else
		  {
			  std::ostringstream ErrorMessage;
				
				ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
				             << "AMQ Session layer failed to create a session for "
										 << "the topic " << TheSubscription.QueueOrTopic
										 << " as requested by the actor " 
										 << TheSubscriber.AsString();
										 
		    throw std::logic_error( ErrorMessage.str() );
		  }
		}
		else
		{
			// There is already a manager for this session type and the subscription 
			// can be registered for this session manager
			
			TheManager->second.AddSubscription( TheSubscription.QueueOrTopic, 
																					TheSubscriber );
		}
	}
	else
  {
		// There was no previous session manager for this type and destination type
		// and both type and destination have to be created; and the successful 
		// creation verified.
		
		auto TypeResult = Session.emplace( TheSubscription.Type );
		
		if ( TypeResult.second )
		{
			// Successful creation of the destination map, and it can then be used 
			// to create the manager.
			
			auto ManagerResult = TypeResult.first->second.emplace( 
													 TheSubscription. DestinationType, 
													 TheSubscription.QueueOrTopic, 
													 TheSubscription.Type, Connection, this );
			
			if ( ManagerResult.second )
				ManagerResult.first->second.AddSubscription( 
					TheSubscription.QueueOrTopic, TheSubscriber );
			else
		  {
				std::ostringstream ErrorMessage;
				
				ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
				             << "AMQ Session layer failed to create a manager for "
										 << "the topic " << TheSubscription.QueueOrTopic
										 << " as requested by the actor "
										 << TheSubscriber.AsString();
										 
			  throw std::logic_error( ErrorMessage.str() );
 			}
		}
		else
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
			             << "AMQ Session Layer failed to create a session for "
									 << "the topic " << TheSubscription.QueueOrTopic
									 << " as requested by the actor "
									 << TheSubscriber.AsString();
									 
		  throw std::logic_error( ErrorMessage.str() );
		}
	}
}

// -----------------------------------------------------------------------------
// Cancelling topic or queue subscriptions
// -----------------------------------------------------------------------------
//
// The session manager should exist in this case, and the cancellation should 
// is just to ask the session manager to remove the subscription and topic if 
// this was the last actor subscribing to this topic.

void Theron::ActiveMQ::SessionLayer::RemoveSubscription(
	const Theron::ActiveMQ::SessionLayer::CancelSubscription & TheCancellation, 
	const Theron::Address TheSubscriber)
{
	auto DestinationMap = Session.find( TheCancellation.Type );
	
	if ( DestinationMap != Session.end() )
  {
		auto TheManager = 
			   DestinationMap->second.find( TheCancellation.DestinationType );
				 
		if ( TheManager != DestinationMap->second.end() )
		{
			TheManager->second.RemoveSubscription( TheCancellation.QueueOrTopic, 
																						 TheSubscriber );
			
			Send( ConfirmCancellation( TheCancellation.QueueOrTopic ), 
						TheSubscriber );
		}
		else
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								   << "AMQ Session Layer could not find the session manager "
									 << "for the requested destination type for topic "
									 << TheCancellation.QueueOrTopic << " requested by the actor "
									 << TheSubscriber.AsString();
									 
		  throw std::logic_error( ErrorMessage.str() );
		}
	}
	else
  {
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
		             << "AMQ Session Layer cound not find the any session for the "
								 << "type requested by the actor " << TheSubscriber.AsString()
								 << " for the topic " << TheCancellation.QueueOrTopic;
								 
	  throw std::logic_error( ErrorMessage.str() );
	}
}

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
//

Theron::ActiveMQ::SessionLayer::SessionLayer( 
	const std::string & BrokerURI, const std::string & UserName,
	const std::string & Password,	 const std::string & Name )
: Actor( Name ), InitialiseLibrary()
{
	// Setting up the connections if they are not already defined
	
	if ( !Factory )
		Factory = 
			std::shared_ptr< cms::ConnectionFactory >( 
				cms::ConnectionFactory::createCMSConnectionFactory( BrokerURI ) );
			
	if ( !Connection )
	{
		if ( UserName.empty() )
			Connection = std::shared_ptr< cms::Connection >( 
										 Factory->createConnection() );
		else
			Connection = std::shared_ptr< cms::Connection >( 
										 Factory->createConnection( UserName, Password ) );
	  
		// The connection needs to be started, but if it is already created, 
	  // it need not be started again. Hence it is only started if the 
		// connection was not already defined, and if the connection was successful
			
		if ( Connection )
			Connection->start();
	}
	
	// Then the message handlers are registered
	
	RegisterHandler( this, &SessionLayer::AddSubscription    );
	RegisterHandler( this, &SessionLayer::RemoveSubscription );
}
