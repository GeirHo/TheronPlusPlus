/*==============================================================================
Active Message Queue network layer

Please see the details in the associated header file.

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
==============================================================================*/

#include <ranges>             // For ranges of messages to send
#include <sstream>            // For nice error messages
#include <source_location>    // For error messages showing code location
#include <stdexcept>          // For standard exceptions
#include <cerrno>             // Standard error codes
#include <system_error>       // Standard error codes
#include <unordered_map>      // Attribute-value objects
#include <vector>             // For passing sequence of parameters

// The Theron++ headers

#include "Communication/NetworkEndpoint.hpp"
#include "Communication/NetworkLayer.hpp"
#include "Communication/AMQ/AMQNetworkLayer.hpp"

// Additional Qpid Proton headers 

#include <proton/delivery_mode.hpp>
#include <proton/target.hpp>
#include <proton/delivery.hpp>
#include <proton/sender_options.hpp>
#include <proton/target_options.hpp>
#include <proton/receiver_options.hpp>
#include <proton/source_options.hpp>
#include <proton/map.hpp>
#include <proton/codec/unordered_map.hpp>

namespace Theron::AMQ
{
/*==============================================================================

 Options and properties

==============================================================================*/
//
// There are several classes dealing with options for the connection, the sender
// the receiver and the messages. Many of these will be application specific 
// and should be provided by the user. Instead offering all options to be given
// to the constructor, the options are centralised to a class construcing the 
// necessary option object on demand.
//
// The first function constructs the connection options used when establishing 
// the connection to the AMQ Broker. This contains information about the 
// connection parameters like the user name and password and certificates for 
// encrypted communication. By default it only sets the desired capabilities 
// for the connection. These are taken from a Red Hat documentation page, 
// and they may or may not have an effect. The idea is that the connection 
// must support topics, these are shared and global.

proton::connection_options 
NetworkLayer::AMQProperties::ConnectionOptions(void) const
{
  proton::connection_options Options;

  Options.desired_capabilities( 
    std::vector< proton::symbol >{ "topic", "shared", "global" } );

  return Options;
}

// The next set of options are applied to each outgoing message. By default
// there will be no properties set. The same goes for the message annotations
// that will also be left empty by default. There is also a function for the 
// delivery annotations, even though it is not clear what these will do or 
// if they will be used.

proton::message::property_map 
NetworkLayer::AMQProperties::MessageProperties( 
const proton::message::property_map & CurrentProperties ) const
{ return CurrentProperties; }

proton::message::annotation_map
NetworkLayer::AMQProperties::MessageAnnotations( 
const proton::message::annotation_map & CurrentAnnotations ) const
{ return CurrentAnnotations; }

proton::message::annotation_map
NetworkLayer::AMQProperties::MessageDelivery( 
const proton::message::annotation_map & CurrentAnnotations ) const
{ return CurrentAnnotations; }

// The sender options are more involved because the sender options also 
// contains options for the target of the messages. The main purpose of the
// default setting is to ensure that the desired form for the publisher is 
// to publish to a topic requiring an acknowledgement from the receiver to 
// delete the message.

proton::sender_options NetworkLayer::AMQProperties::SenderOptions( void ) const
{
  proton::sender_options LinkParameters;
  proton::target_options TopicParameters;
    
  TopicParameters.capabilities( std::vector< proton::symbol >{ "topic" } );
  TopicParameters.durability_mode( proton::terminus::durability_mode::UNSETTLED_STATE );

  LinkParameters.delivery_mode( proton::delivery_mode::AT_LEAST_ONCE );
  LinkParameters.target( TopicParameters );

  return LinkParameters;
}

// Receivers have their own set of parameters, and some of these are again other
// parameter classes. Otherwise the options are very much equal to the other 
// default options.

proton::receiver_options 
NetworkLayer::AMQProperties::ReceiverOptions( void ) const 
{
  proton::receiver_options LinkParameters;
  proton::source_options   TopicParameters;

  TopicParameters.capabilities( 
    std::vector< proton::symbol >{ "topic", "shared", "global" } );
  TopicParameters.distribution_mode( proton::source::distribution_mode::COPY );
  TopicParameters.durability_mode( proton::terminus::durability_mode::UNSETTLED_STATE );
  TopicParameters.expiry_policy( proton::source::NEVER );
  
  LinkParameters.delivery_mode( proton::delivery_mode::AT_LEAST_ONCE );
  LinkParameters.source( TopicParameters );

  return LinkParameters;
}

/*==============================================================================

 Support functions

==============================================================================*/
//
// The support functions are used to create the senders and receivers for given
// topic names since the process involves setting the options for the senders
// and receivers to ensure that topics are created if the given target topic 
// does not exist already on the message broker.

void NetworkLayer::CreateSender( const TopicName & TheTarget )
{
  proton::sender_options LinkParameters( Properties->SenderOptions() );
  
  LinkParameters.name( TheTarget );
  
  auto [ PublisherHandler, Success ] = Publishers.emplace( TheTarget, 
         AMQBroker.open_sender( "topic://" + TheTarget, LinkParameters ) );

  if ( Success ) PublisherHandler->second.open();
}

void NetworkLayer::CreateReceiver(const TopicName & TheTarget)
{
  proton::receiver_options LinkParameters( Properties->ReceiverOptions() );

  LinkParameters.name( TheTarget );
    
  auto [ SubscriberHandler, Success ] = Subscribers.emplace( TheTarget, 
         AMQBroker.open_receiver("topic://" + TheTarget, LinkParameters ) );
  
  if( Success ) SubscriberHandler->second.open();
}

// The Send message function is called from the work queue and can therefore 
// freely use the publisher map. The message is sent if the publisher exist
// for the topic and the link has credits. If no credit, the message will 
// just be queued. If there is not publisher for the target topic, it will 
// be created and the message will be queued.

void NetworkLayer::SendMessage( const TopicName & TargetTopic, 
                   const std::shared_ptr< proton::message > & TheMessage )
{
  auto PublisherHandler = Publishers.find( TargetTopic );

  // Adding the default values to the message properties and annotations

  TheMessage->properties()           
    = Properties->MessageProperties( TheMessage->properties() );
  TheMessage->message_annotations()  
    = Properties->MessageAnnotations( TheMessage->message_annotations() );
  TheMessage->delivery_annotations() 
    = Properties->MessageDelivery( TheMessage->message_annotations() );

  if( PublisherHandler != Publishers.end() )
  {
    if(  PublisherHandler->second.active() && 
        (PublisherHandler->second.credit() >0) )
      PublisherHandler->second.send( *TheMessage );
    else
    {
      std::lock_guard< std::mutex > TheLock( MessageCacheLock );
      MessageCache.emplace( TargetTopic, TheMessage );
    }
  }
  else
  {
    std::lock_guard< std::mutex > TheLock( MessageCacheLock );
    CreateSender( TargetTopic );
    MessageCache.emplace( TargetTopic, TheMessage );
  }
}

/*==============================================================================

 Communication event handlers

==============================================================================*/
//
// When the AMQ event loop has started, the connection to the AMQ broker is 
// started. 

void NetworkLayer::on_container_start( proton::container & TheLoop )
{}

// When it is confirmed that the connection is open, this is simply recorded 
// in the boolean variable to indicate that the network interface is 
// operational. Note that it also requests to start a receiver and publisher
// for the standard actor discovery topic.

void NetworkLayer::on_connection_open( proton::connection & TheBroker )
{
  Connected = true;

  CreateReceiver( DiscoveryTopic ); 
  CreateSender  ( DiscoveryTopic ); 
}

// When a new sender has been connected to the right topic on the AMQ broker
// there may be one or more messages in the cache waiting for this sender to 
// be ready for use and actions to send these messages will be inserted in 
// the event loop's action queue. 
// 
// The messages must be copied into the action function as the cache entries 
// should be removed at this point. Note that the reply-to address is already 
// set for the message in the cache and the topic name is deduced from the 
// active sender. The issue is that there is a credit on the link and so 
// only as many messages as there are credits can be sent.
//
// The lock is needed on the message cache since the Network Layer Actor may 
// receive new messages for this publisher at any time, and these will be 
// added to the cache from the Actor's thread that could create a conflict. 
// A lock is therefore used and the lock guard object will automatically 
// release the lock when the function terminates.

void NetworkLayer::on_sendable( proton::sender & ThePublisher )
{
  proton::target                TheTopicID( ThePublisher.target() );
  TopicName                     TheName( TheTopicID.address() );
  std::lock_guard< std::mutex > TheLock( MessageCacheLock );

  if( TheName.starts_with( "topic://" ) ) TheName.erase(0,8);

  // Search for the cached messages awaiting the creation of this sender and 
  // forward them as delayed actions to the AMQ event loop.

  while( (ThePublisher.credit() > 0) && MessageCache.contains( TheName ) )
  {
    auto MessageRecord = MessageCache.find( TheName );
    ThePublisher.send( *(MessageRecord->second) );
    MessageCache.erase( MessageRecord );
  }  
}

// When a deferred action creates the receiver, and the receiver has success-
// fully connected to the AMQ Broker, the receiver will be stored in the 
// subscriber list under topic address of the target topic.

void NetworkLayer::on_receiver_open( proton::receiver & TheSubscription )
{}

// When a message arrives on one of the subscribed topics, the message to send 
// back to the session layer is created, and forwarded using the Actor's 
// send function. The sender's address is taken from the received proton 
// message reply-to field.
// 
// There are two situations to consider for a message on the discovery topic:
// If the sender is this endpoint, then the message is just discarded, and if 
// it comes from a different endpoint, it will send the received information 
// to the Session Layer as an actor discovery message.

void NetworkLayer::on_message( proton::delivery & DeliveryStatus, 
                               proton::message  & TheMessage )
{
  TopicName ReceiverTopic( TheMessage.to() );

  if( ReceiverTopic.starts_with( "topic://" ) ) ReceiverTopic.erase(0,8);

  if ( ReceiverTopic == DiscoveryTopic )
  {
    if ( TheMessage.reply_to() != GetAddress().AsString() )
    {       
      switch ( Protocol::Command( TheMessage.subject() ) )
      {
        case Protocol::Action::ActorRemoval:
          {
            // The global address of the remote actor is stored in the body of
            // the received message, and it is just to inform the Session Layer
            // that this actor is gone, and remove any publisher this endpoint 
            // may have for the actor
            
            MessageType::AddressType 
            ActorAddress( proton::get< std::string >( TheMessage.body() ) );
            
            Send(  RemoveActor( ActorAddress ), 
                   Network::GetAddress( Network::Layer::Session ) );
            Publishers.erase( ActorAddress  );
          }
          break;
        case Protocol::Action::GlobalAddress:
          {
            // The global address for an actor has been resolved, and this 
            // information is just sent to the Session Layer to be recorded if
            // any local Actor would like to send messages to this remote Actor
            // in the future
            
            std::unordered_map< std::string, std::string > ActorAddresses;
            proton::get( TheMessage.body(), ActorAddresses );
          
            Send( ResolutionResponse( ActorAddresses.at("RequestedActor"), 
                                      ActorAddresses.at("RequestingActor") ), 
                  Network::GetAddress( Network::Layer::Session ) );
          }
          break;
        case Protocol::Action::ResolveAddress:
          {
            // A remote endpoint is looking for the global address of an Actor
            // and this request is forwarded to the resolution layer. If the 
            // Actor is on this endpoint, a resolution response will be 
            // generated by the Session Layer, and if the requested actor is 
            // not on this node, the message is just ignored by the Session 
            // Layer and no response will be generated.
            
            std::unordered_map< std::string, std::string > ActorAddresses;
            proton::get( TheMessage.body(), ActorAddresses );

            Send( ResolutionRequest( ActorAddresses.at("RequestedActor"), 
                                     ActorAddresses.at("RequestingActor") ), 
                  Network::GetAddress( Network::Layer::Session ) ); 
          }
          break;
        case Protocol::Action::EndpointShutDown:
          {
            // When a remote endpoint is shutting down it will send a message 
            // on the discovery channel with the global address of the closing 
            // endpoint as the message body. Any subscriber that this endpoint 
            // may have against the Actors on the closing endpoint should be 
            // removed. 
            
            MessageType::AddressType 
            ClosingEndpoint( proton::get< std::string >( TheMessage.body() ) );
            
            Send( Network::ClosingEndpoint( ClosingEndpoint ), 
                  Network::GetAddress( Network::Layer::Session ) ); 
          
            // Then remove all subscriptions held against this endpoint
          
            std::erase_if( Subscribers, [&](const auto & TheSubscriber){
              AMQ::GlobalAddress SubscriberAddress( TheSubscriber.first );
              return SubscriberAddress.Endpoint() == ClosingEndpoint.Endpoint();
            });
          }
          break;
      }
    }
  }
  else
  {
    TopicName SenderTopic( TheMessage.reply_to() );

    if( SenderTopic.empty() )
      SenderTopic = ReceiverTopic;
    else if( SenderTopic.starts_with("topic://") )
      SenderTopic.erase(0,8);

    TheMessage.reply_to( SenderTopic );
    
    Theron::AMQ::Message 
    Inbound( SenderTopic, ReceiverTopic, 
             std::make_shared< proton::message >( TheMessage ) );
  
    Send( Inbound, Network::GetAddress( Network::Layer::Session ) );
  }
  
  DeliveryStatus.accept();
}

// -----------------------------------------------------------------------------
// Error handlers
// -----------------------------------------------------------------------------
//
// The connection error is created when it is no longer possible to connect 
// to the message broker after several iterations of reconnect attempts. This 
// is therefore a unrecoverable error, and we will just throw this as an 
// exception since this should not happen in normal systems.

void NetworkLayer::on_connection_error( proton::connection & TheBroker )
{
  std::source_location Location = std::source_location::current();
  std::ostringstream ErrorMessage;

	ErrorMessage << "[" << Location.file_name() << " at line " << Location.line() 
							 << "in function " << Location.function_name() <<"] " 
               << "Permanent Actvive Message Queue (AMQ) connection error: "
							 <<  TheBroker.error().what();
               
  // Throwing the exception indicating that this is a connection error of 
  // the system category with the error code indicating that the connection 
  // was aborted.
               
  throw std::system_error( static_cast<int>( std::errc::connection_aborted ), 
                           std::system_category(), ErrorMessage.str() );
}

// There is a second error handler that is called for any other definite and 
// unrecoverable error. It basically forwards the error condition from the 
// Qpid Proton adding the standard information about where this error handler 
// can be found.

void NetworkLayer::on_error( const proton::error_condition & TheError )
{
  std::source_location Location = std::source_location::current();
  std::ostringstream ErrorMessage;

	ErrorMessage << "[" << Location.file_name() << " at line " << Location.line() 
							 << "in function " << Location.function_name() <<"] " 
               << "General Actvive Message Queue (AMQ) API error: "
							 <<  TheError.what();
               
  // Throwing the exception indicating that this is a connection error of 
  // the system category with the error code indicating that the AMQ protocol is 
  // not available.
               
  throw std::system_error( static_cast<int>( std::errc::no_protocol_option ), 
                           std::system_category(), ErrorMessage.str() );
}

/*==============================================================================

 Actor system sender and receiver management

==============================================================================*/
//
// In order to send the commands over the network they must be converted back 
// and forth to strings and the two operators doing this are defined here.

NetworkLayer::Protocol::Action 
NetworkLayer::Protocol::Command( const std::string & CommandText )
{
  static std::unordered_map< std::string, Action > StringToCommand{
    {"ResolveAddress",   Action::ResolveAddress   },
    {"GlobalAddress",    Action::GlobalAddress    },
    {"ActorRemoval",     Action::ActorRemoval     },
    {"EndpointShutdown", Action::EndpointShutDown }
  };
  
  return StringToCommand.at( CommandText );
}
  
std::string
NetworkLayer::Protocol::String( NetworkLayer::Protocol::Action TheCommand)
{
  static std::unordered_map< Action, std::string > CommandToString{
    {Action::ResolveAddress,   "ResolveAddress"   },
    {Action::GlobalAddress,    "GlobalAddress"    },
    {Action::ActorRemoval,     "ActorRemoval"     },
    {Action::EndpointShutDown, "EndpointShutdown" }
  };

  return CommandToString.at( TheCommand );
}

// The resolution request is sent from the Session Layer if an actor address 
// does not have a known global address. This means that a request should 
// be sent on the discovery  topic to remote endpoints and that the remote 
// endpoint will respond with the full Actor address. This will generate a 
// message with the actor name as the  payload string, and the resolve address 
// command will be the message subject.
// 
// There is a strange issue with the property function as it will not work 
// if the message is encapsulated in a shared pointer. This means that the 
// message must be constructed first and then copied to a shared pointer 

void NetworkLayer::ResolveAddress( 
  const ResolutionRequest & TheRequest,
	const Address TheSessionLayer )
{
  std::unordered_map< std::string, std::string > ActorAddresses{
    { "RequestedActor", TheRequest.RequestedActor.AsString()    },
    { "RequestingActor", TheRequest.RequestingActor.AsString() }
  };

  proton::message AMQRequest;  

  // Setting the header fields and the message body

  AMQRequest.to( DiscoveryTopic );
  AMQRequest.reply_to( GetAddress().AsString() );
  AMQRequest.subject( Protocol::String( Protocol::Action::ResolveAddress ) );
  AMQRequest.body() = ActorAddresses;

  // Creating a copy of the filled message and send it to the other endpoints

  std::shared_ptr< proton::message > RequestToSend = 
    std::make_shared< proton::message >( AMQRequest );

  ActionQueue.add( [=,this](){ 
    SendMessage( DiscoveryTopic, RequestToSend ); 
  });
}

// If the sought Actor is on this node, the Session Layer will respond with a 
// resolution response request containing the global address of the local Actor.
// This global address is also the name of the topic for inbound messages to 
// the actor, and the local subscriber to the topic will be created before the 
// discovery message is sent to minimise the delay of receiving the inbound 
// messages. Remember, however, that even thought the request to create the 
// receiver is placed before resolution response is sent, there is no guarantee
// that the receiver will be active when the remote endpoint starts sending 
// messages for the newly discovered Actor.

void NetworkLayer::ResolvedAddress( 
  const ResolutionResponse & TheResponse,
	const Address TheSessionLayer )
{
  std::unordered_map< std::string, std::string > ActorAddresses{
    { "RequestedActor", TheResponse.RequestedActor.AsString()  },
    { "RequestingActor", TheResponse.RequestingActor.AsString() }
  };
  
  proton::message AMQResponse;

  // Setting the header fields and the message body

  AMQResponse.to( DiscoveryTopic );
  AMQResponse.reply_to( GetAddress().AsString() );
  AMQResponse.subject( Protocol::String( Protocol::Action::GlobalAddress ) );
  AMQResponse.body() = ActorAddresses;

  std::shared_ptr< proton::message > ResponseToSend = 
    std::make_shared< proton::message >( AMQResponse );

  // The requested actor is on this node, and the receiver must be opened to 
  // get the messages to come from the remote requesting actor.
  
  ActionQueue.add(
    [=,this]()
    { CreateReceiver( ActorAddresses.at("RequestedActor") ); }
  );
  
  ActionQueue.add(
    [=,this](){ SendMessage( DiscoveryTopic, ResponseToSend ); }
  );
}

// The actor removal handler notifies all remote endpoints that the Actor is 
// closing and local subscribers for its channel should be removed. Furthermore,
// the local publisher for this actor is removed on this node.

void NetworkLayer::ActorRemoval( 
  const RemoveActor & TheCommand,
  const Address TheSessionLayer )
{
  proton::message ActorClosing;
  
  // Setting the header fields and the message body

  ActorClosing.to( DiscoveryTopic );
  ActorClosing.reply_to( GetAddress().AsString() );
  ActorClosing.subject( Protocol::String( Protocol::Action::ActorRemoval ) );
  ActorClosing.body( TheCommand.GlobalAddress.AsString() );
  
  std::shared_ptr< proton::message > ClosingMessage =
    std::make_shared< proton::message >( ActorClosing );

  // It will no longer be possible to reach this Actor and this is communicated
  // to the remote endpoints and the local receiver is closed.
  
  ActionQueue.add(
    [=,this](){ SendMessage( DiscoveryTopic, ClosingMessage ); }
  );
  
  ActionQueue.add(
    [=,this](){ Subscribers.erase( TheCommand.GlobalAddress.AsString() ); }
  );

}

/*==============================================================================

 Message handling

==============================================================================*/
//
// The handler for the outbound messages will first check if there is a 
// publisher available for the recipient's global address (the AMQ topic)
// the message send action will just be queued. If this is not the case, 
// then there will be a an action set to start the sender and the message is 
// cached to be sent when the sender object is connected and active. 

void NetworkLayer::OutboundMessage( const AMQ::Message & TheMessage, 
                                    const Address TheSessionLayer )
{
  std::lock_guard< std::mutex >      TheLock( MessageCacheLock );
  std::shared_ptr< proton::message > RemoteMessage( TheMessage.GetPayload() );
  
  TopicName DestinationActor( TheMessage.GetRecipient().AsString() );
  
  RemoteMessage->reply_to( TheMessage.GetSender().AsString() );
  RemoteMessage->to( DestinationActor );
  
  ActionQueue.add( [=, this](){ 
      SendMessage( DestinationActor, RemoteMessage ); });
}

/*==============================================================================

 Topic subscriptions 

==============================================================================*/

void NetworkLayer::ManageTopics( const TopicSubscription & TheMessage, 
                                 const Address TheSessionLayer )
{
  switch( TheMessage.Command )
  {
    case TopicSubscription::Action::Subscription:
      ActionQueue.add( [=,this](){
        CreateReceiver( TheMessage.TheTopic );
      });
      break;
    case TopicSubscription::Action::Publisher:
      ActionQueue.add( [=, this](){ 
        CreateSender( TheMessage.TheTopic ); 
      });
      break;
    case TopicSubscription::Action::CloseSubscription:
      ActionQueue.add( [=,this](){
        Subscribers.erase( TheMessage.TheTopic );
      });
      break;
    case TopicSubscription::Action::ClosePublisher:
      ActionQueue.add( [=,this](){
        Publishers.erase( TheMessage.TheTopic );
      });
      break;
  }
}

/*==============================================================================

 Initialisation and closing

==============================================================================*/
//
// The constructor function initialises the local variables to given or default
// values, and start the event loop
// 

NetworkLayer::NetworkLayer( 
  const Theron::AMQ::GlobalAddress & EndpointName,
	const std::string & BrokerURL,
	const unsigned int & Port,
  const std::shared_ptr< AMQProperties > GivenProperties )
: Actor( EndpointName.AsString() ),
  StandardFallbackHandler( EndpointName.AsString() ),
  Theron::NetworkLayer< AMQ::Message >( EndpointName.AsString() ),
  proton::messaging_handler(),
  AMQEventLoop( *this, EndpointName.AsString() ), EventLoopExecuter(), 
  AMQConnection(), 
  Connected( false ), Publishers(), Subscribers(), MessageCache(), 
  ActionQueue( AMQEventLoop ),
  EndpointString( EndpointName.Endpoint() ),
  Properties( GivenProperties )
{
  // First define the URL string for the connection and store it.
  
  std::ostringstream URLString;
  
  URLString << BrokerURL << ":" << Port;
  AMQBrokerURL = URLString.str();
  
  // The connection is established and opened. Then a session for the 
  // publishers and subscribers is created for this connection.
  
  AMQConnection = AMQEventLoop.connect( AMQBrokerURL, 
                                        Properties->ConnectionOptions() );
  AMQConnection.open();
  //AMQBroker = AMQConnection.open_session();
  AMQBroker = AMQConnection.default_session();

  // Then the event loop is started and the rest of the processing will be 
  // done by the various message handlers and event handlers
  
  EventLoopExecuter = std::thread( [&](){ AMQEventLoop.run(); } );

  // Most of the message handlers are already registered by the generic 
  // Network Layer base class and overridden here, and only one new message
  // handler must be registered.

  RegisterHandler( this, &NetworkLayer::ManageTopics );
}

// The main closing happens when the Network Layer receives a stop message. 
// It will then call stop the event loop, and join the event loop execution 
// thread to wait until all open connections have been closed and the event 
// loop has terminated. The event loop stop function is thread safe and can 
// be called from the Actors message processing thread. Note that it will first
// send a closing message to the control channel so that remote endpoints will 
// know that all actors on this endpoint will be unreachable.

void NetworkLayer::Stop( const Network::ShutDown & StopMessage,
										     const Address Sender )
{
  // Mark the connection as closed

  Connected = false;

  // Send the closing message to all other endpoints

  std::shared_ptr< proton::message > ClosingMessage;
  
  ClosingMessage->reply_to( GetAddress().AsString() );
  ClosingMessage->to( DiscoveryTopic );
  ClosingMessage->subject(Protocol::String(Protocol::Action::EndpointShutDown));
  ClosingMessage->body( GetAddress().AsString() );
  
  ActionQueue.add(
    [=,this](){ SendMessage( DiscoveryTopic, ClosingMessage ); }
  );

  ActionQueue.add( [this](){ std::ranges::for_each( Publishers, 
              [](auto & PublisherRecord){ PublisherRecord.second.close(); });
  });
  ActionQueue.add( [this](){ std::ranges::for_each( Subscribers, 
              [](auto & SubscriberRecord){ SubscriberRecord.second.close(); });
  });
  ActionQueue.add( [this](){ Publishers.clear(); });
  ActionQueue.add( [this](){ Subscribers.clear(); });
  
  // Request the event loop to stop and wait for it to finish processing all 
  // pending actions and actually close.
  
  AMQBroker.close();
  AMQConnection.close();
  AMQEventLoop.stop();
  if ( EventLoopExecuter.joinable() )
    EventLoopExecuter.join();
}

// The destructor is simply invoking the previous stop function, before 
// terminating allowing the destructor of each data element or base class to 
// clean up.

NetworkLayer::~NetworkLayer( void )
{
  Stop( Network::ShutDown(), GetAddress() );
}

} // End name space Theron AMQ
