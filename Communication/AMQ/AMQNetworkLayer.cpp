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

 Support functions

==============================================================================*/
//
// The support functions are used to create the senders and receivers for given
// topic names since the process involves setting the options for the senders
// and receivers to ensure that topics are created if the given target topic 
// does not exist already on the message broker.

void NetworkLayer::CreateSender(const TopicName & TheTarget)
{
  proton::sender_options LinkParameters;
  proton::target_options TopicParameters;
    
  TopicParameters.capabilities( std::vector< proton::symbol >{ "topic" } );
  TopicParameters.durability_mode( proton::terminus::durability_mode::UNSETTLED_STATE );
  LinkParameters.delivery_mode( proton::delivery_mode::AT_LEAST_ONCE );
  LinkParameters.target( TopicParameters );
    
  AMQBroker.open_sender( TheTarget, LinkParameters );
}

void NetworkLayer::CreateReceiver(const TopicName & TheTarget)
{
  proton::receiver_options LinkParameters;
  proton::source_options   TopicParameters;

  TopicParameters.capabilities( std::vector< proton::symbol >{ "topic" } );
  TopicParameters.distribution_mode( proton::source::distribution_mode::COPY );
  TopicParameters.durability_mode( proton::terminus::durability_mode::UNSETTLED_STATE );
  LinkParameters.delivery_mode( proton::delivery_mode::AT_LEAST_ONCE );
  LinkParameters.source( TopicParameters );
  
  AMQBroker.open_receiver( TheTarget, LinkParameters );
}

/*==============================================================================

 Communication event handlers

==============================================================================*/
//
// When the AMQ event loop has started, the connection to the AMQ broker is 
// started. 

void NetworkLayer::on_container_start( proton::container & TheLoop )
{
   TheLoop.connect( AMQBrokerURL, ConnectionOptions );
}

// When it is confirmed that the connection is open, this is simply recorded 
// in the boolean variable to indicate that the network interface is 
// operational. Note that it also requests to start a receiver and publisher
// for the standard actor discovery topic.

void NetworkLayer::on_connection_open( proton::connection & TheBroker )
{
  AMQBroker = TheBroker;
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
// active sender.
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
  
  // Add the topic and the sender to the Publishers. Because of asynchronous
  // behaviour on this enpoint, it could be that he same publisher is created
  // several times, and so it should only be registered the first time.
  
  Publishers.try_emplace( TheName, ThePublisher );
  
  // Search for the cached messages awaiting the creation of this sender and 
  // forward them as delayed actions to the AMQ event loop.
  
  auto [First, Last] = MessageCache.equal_range( TheName );
  
  if( First != MessageCache.end() )
  {
    for ( auto & [_, MessagePtr] : std::ranges::subrange( First, Last ) )
      ThePublisher.send( *MessagePtr ); 
    
    // Since the messages was copied by the action functions, they can be
    // removed from the cache
    
    MessageCache.erase( First, Last );
  }
}

// When a deferred action creates the receiver, and the receiver has success-
// fully connected to the AMQ Broker, the receiver will be stored in the 
// subscriber list under topic address of the target topic.

void NetworkLayer::on_receiver_open( proton::receiver & TheSubscription )
{
  Subscribers.try_emplace( TheSubscription.target().address(), 
                           TheSubscription );
}

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
  if ( TheMessage.to() == DiscoveryTopic )
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
            Publishers.erase( ActorAddress );
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
    Theron::AMQ::Message 
    Inbound( TheMessage.reply_to(), TheMessage.to(), 
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
               
  throw std::system_error( ECONNABORTED, std::system_category(), 
                           ErrorMessage.str() );
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
               
  throw std::system_error( ENOPROTOOPT, std::system_category(), 
                           ErrorMessage.str() );
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
// command will be the message subject

void NetworkLayer::ResolveAddress( 
  const ResolutionRequest & TheRequest,
	const Address TheSessionLayer )
{
  std::unordered_map< std::string, std::string > ActorAddresses{
    { "RequestedActor", TheRequest.RequestedActor.AsString()    },
    { "RequestingActor", TheRequest.RequestingActor.AsString() }
  };

  proton::message AMQRequest;  
  AMQRequest.properties() = MessageProperties;
  AMQRequest.to( DiscoveryTopic );
  AMQRequest.reply_to( GetAddress().AsString() );
  AMQRequest.subject( Protocol::String( Protocol::Action::ResolveAddress ) );
  AMQRequest.body() = ActorAddresses;

  ActionQueue.add(
    [=,this](){ Publishers[ DiscoveryTopic ].send( AMQRequest ); }
  );
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
  AMQResponse.properties() = MessageProperties;
  AMQResponse.to( DiscoveryTopic );
  AMQResponse.reply_to( GetAddress().AsString() );
  AMQResponse.subject( Protocol::String( Protocol::Action::GlobalAddress ) );
  AMQResponse.body() = ActorAddresses;
  
  // The requested actor is on this node, and the receiver must be opened to 
  // get the messages to come from the remote requesting actor.
  
  ActionQueue.add(
    [=,this]()
    { CreateReceiver( ActorAddresses.at("RequestedActor") ); }
  );
  
  ActionQueue.add(
    [=,this](){ Publishers[ DiscoveryTopic ].send( AMQResponse ); }
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
  
  ActorClosing.properties() = MessageProperties;
  ActorClosing.to( DiscoveryTopic );
  ActorClosing.reply_to( GetAddress().AsString() );
  ActorClosing.subject( Protocol::String( Protocol::Action::ActorRemoval ) );
  ActorClosing.body( TheCommand.GlobalAddress.AsString() );
  
  // It will no longer be possible to reach this Actor and this is communicated
  // to the remote endpoints and the local receiver is closed.
  
  ActionQueue.add(
    [=,this](){ Publishers[ DiscoveryTopic ].send( ActorClosing ); }
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
  
  RemoteMessage->properties() = MessageProperties;
  RemoteMessage->reply_to( TheMessage.GetSender().AsString() );
  RemoteMessage->to( DestinationActor );
  
  if ( Publishers.contains( DestinationActor ) )
    ActionQueue.add( [=, this](){ 
      Publishers[ DestinationActor ].send( *RemoteMessage ); 
    });
  else
  {
    MessageCache.emplace( DestinationActor, RemoteMessage );
    
    ActionQueue.add( [=, this](){ 
      CreateSender( DestinationActor );
    });
  }
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
  const proton::connection_options & GivenConnectionOptions,
  const proton::message::property_map & GivenMessageOptions )
: Actor( EndpointName.AsString() ),
  StandardFallbackHandler( EndpointName.AsString() ),
  Theron::NetworkLayer< AMQ::Message >( EndpointName.AsString() ),
  proton::messaging_handler(),
  AMQEventLoop( EndpointName.AsString() ), EventLoopExecuter(), AMQBroker(), 
  Connected( false ), Publishers(), Subscribers(), MessageCache(), 
  ActionQueue( AMQEventLoop ),
  ConnectionOptions( GivenConnectionOptions ),  
  MessageProperties( GivenMessageOptions ),
  EndpointString( EndpointName.Endpoint() )
{
  // First define the URL string for the connection and store it.
  
  std::ostringstream URLString;
  
  URLString << BrokerURL << ":" << Port;
  AMQBrokerURL = URLString.str();
  
  // The handler for the connection options is set to this network layer 
  // class. It could not be set in the initialisation section above since 
  // the 'this' pointer does not formally exist before after the initialisation
  // block.
  
  ConnectionOptions.handler( *this );
  
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

  proton::message ClosingMessage;
  
  ClosingMessage.properties() = MessageProperties;
  ClosingMessage.reply_to( GetAddress().AsString() );
  ClosingMessage.to( DiscoveryTopic );
  ClosingMessage.subject(Protocol::String(Protocol::Action::EndpointShutDown));
  ClosingMessage.body( GetAddress().AsString() );
  
  ActionQueue.add( [=,this](){ 
    Publishers[ DiscoveryTopic ].send( ClosingMessage ); });
  
  // Delete the subscribers and publishers of this endpoint
  
  ActionQueue.add( [this](){ Subscribers.clear(); });
  ActionQueue.add( [this](){ Publishers.clear(); });
  
  // Request the event loop to stop and wait for it to finish processing all 
  // pending actions and actually close.
  
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
