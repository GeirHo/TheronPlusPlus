/*==============================================================================
Active Message Queue: Network Layer

The Active Message Queue (AMQ) [1] is a server based messaging system where
various clients can exchange messages via a server (message broker).

The Network Layer takes care of the interface to the AMQ server (broker) based
on the Qpid Proton [2] Application Programming Interface (API) [3]. The basic
idea is that the Network Layer will encapsulate all Proton activities and be 
the handler for all Proton call-backs. The Network Layer will first connect 
to the AMQ broker, and start the Proton event manager. Then it will keep two 
maps of AMQ topics, one for publishing messages and one for receiving messages.
When a message is sent to topic for which there is no sender, the sender 
will be created and then the message will be sent as soon as the sender is 
connected and active. Messages sent to the remote actor corresponding to this 
sender during the initialisation of the sender will be forwarded sequentially 
when the sender starts. 

Each local actor has a local receiver as its global mailbox for external 
messages. These local receivers are created at the end of the address resolution
protocol when a remote actor indicates an available message for a destination 
actor, and the network layer of the endpoint hosting the destination actor 
gets the confirmation from the Session Layer that the actor is on this node. 

Both senders and receivers will remain open as long as there are local actors 
using them meaning that the Session Layer will inform the Network Layer that 
an actor closes, and then the Network Layer will inform other endpoints and 
close the publisher associated with the closing actor. The remote endpoints 
should remove potential subscribers set for the closing actors.

The Session Layer must explicitly subscribe to topics. All subscriptions will be 
of the 'exactly once' type meaning that each message will be delivered with 
link level flow control and only once. This is the strongest delivery guarantee
possible.

Subscriptions are also explicitly closed by the Session Layer when the last 
local Actor holding a subscription unsubscribes, for instance when that Actor 
closes. The session layer will also close the sender when the last local 
Actor having used the sender closes. 

References:
[1] http://activemq.apache.org/
[2] https://qpid.apache.org/proton/index.html
[3] https://qpid.apache.org/releases/qpid-proton-0.39.0/proton/cpp/api/index.html

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
==============================================================================*/

#ifndef THERON_AMQ_NETWORK_LAYER
#define THERON_AMQ_NETWORK_LAYER

// Standard headers

#include <memory>        // For smart pointers
#include <string>        // For standard strings
#include <unordered_map> // For O(1) lookups of topics
#include <map>           // For outbound message cache
#include <thread>        // For running the messaging event loop
#include <mutex>         // For thread safe message caching
#include <string_view>   // For references to strings

// Qpid Proton headers

#include <proton/container.hpp>           // Proton event engine
#include <proton/messaging_handler.hpp>   // Event call-backs
#include <proton/connection.hpp>          // All connections to the broker
#include <proton/connection_options.hpp>  // Options for the broker connection
#include <proton/session.hpp>             // Session for hosting sender and receivers
#include <proton/session_options.hpp>     // Options for the session
#include <proton/work_queue.hpp>          // Queue of pending send operations
#include <proton/message.hpp>             // AMQ message format
#include <proton/receiver.hpp>            // The subscriber object
#include <proton/receiver_options.hpp>    // The receiver options
#include <proton/sender.hpp>              // The sender object
#include <proton/sender_options.hpp>      // The sender options

// Headers for the Theron++ actor system

#include "Actor.hpp"
#include "Utility/StandardFallbackHandler.hpp"

// Headers for the Theron++ communication layers

#include "Communication/LinkMessage.hpp"
#include "Communication/NetworkLayer.hpp"

// Active Message Queue network headers

#include "Communication/AMQ/AMQMessage.hpp"

namespace Theron::AMQ
{
// The AMQ topic names are just standard strings.

using TopicName = std::string; 

// Setting the hard coded topic for all endpoints to listen to.

constexpr std::string DiscoveryTopic{ "TheronPlusPlus" };
  
/*==============================================================================

 Network layer

==============================================================================*/
//
// The class is an Actor implementing the generic Network Layer for the AMQ 
// protocol. It is also a message handler for the active container, and 
// exchange messages with the Session Layer on a strict topic name base.
//
// This class is also provide the event handlers for the AMQ event loop and 
// it is therefore a messaging handler even though it also encapsulates the 
// other communication objects.

class NetworkLayer
: virtual public Actor,
  virtual public StandardFallbackHandler,
  virtual public Theron::NetworkLayer< Message >,
  virtual public proton::messaging_handler
{
  // --------------------------------------------------------------------------
	// Connectivity related variables
	// --------------------------------------------------------------------------
	//
  // The first variables are the event loop state machine, the thread to 
  // execute this event loop, and the connection object managing the connection
  // with the broker. There is also a flag to indicate the status of the 
  // connection.
  
private:
  
  proton::container  AMQEventLoop;
  std::thread        EventLoopExecuter;
  proton::connection AMQConnection;
  proton::session    AMQBroker;
  bool               Connected;
  
  // The actual publishers and subscribers are kept in separate unordered 
  // maps with the AMQ topic as the lookup key and the corresponding proton 
  // class as destination.
  
  std::unordered_map< TopicName, proton::sender   > Publishers;
  std::unordered_map< TopicName, proton::receiver > Subscribers;
  
  // The sender for a message is under the control of the event loop, and 
  // messages that requires a new publisher to be established because it is 
  // the first message for a topic must wait for that sender to connect 
  // to the remote AMQ broker before the message can be sent. This means that 
  // the message cache can be used by two threads; the event loop and the 
  // Actor. It must therefore be protected by a mutex to ensure that only 
  // one thread will manipulate the cache at the same time.
  
  std::mutex MessageCacheLock;
  std::multimap< TopicName, std::shared_ptr< proton::message > > MessageCache;
  
  // The event loop will take actions from the action queue and adding
  // operations to the queue is thread safe. An action can be to send a 
  // message, or it can be an action to create a receiver subscribing to a 
  // topic. 
  
  proton::work_queue ActionQueue;
  
  // The URL of the AMQ broker is stored if it is given. Note that the other
  // options to connect, like user and password and security policies are 
  // given as a part of the connection options that must be given to the 
  // class constructor.
  
  std::string AMQBrokerURL;
  
  // Finally, it keeps the endpoint string to be able to add that to local 
  // actor names when a resolution request arrives for a local actor.
  
  const std::string EndpointString;
  
  // ---------------------------------------------------------------------------
	// Support functions
	// ---------------------------------------------------------------------------
	//
  // There are two types of AMQ targets for messages: Queues or topics. The 
  // former is used for direct communication, and the second is used for many 
  // to many. The AMQ Broker should be instructed to create new queues or topics
  // if a sender or receiver is created for a label that does not already exist.
  // However, the exact behaviour depends on the server configuration, and to 
  // ensure that only topics will be used, explicit settings will be used when
  // the sender or receiver is created. To avoid duplication of code, support
  // functions are used. For more details, see
  // https://access.redhat.com/documentation/en-us/red_hat_amq_clients/2.11/html/using_the_amq_cpp_client/senders_and_receivers

  void CreateSender  ( const TopicName & TheTarget );
  void CreateReceiver( const TopicName & TheTarget );

  void SendMessage( const TopicName & TargetTopic, 
                    const std::shared_ptr< proton::message > & TheMessage );

  // ---------------------------------------------------------------------------
	// Property management
	// ---------------------------------------------------------------------------
	//
  // There are different parameters used when creating subscribers or publisers
  // or when sending messages. Normally, these will be static during the 
  // application execution, but they do not have to be, and a user of the 
  // class may want to implement own defaults or even dynamic versions of the
  // properties. The solution is therefore to enable the property management 
  // as a separate class with methods that can be overridden by any user 
  // class, but where the default settings are provided by the basic class 
  // functions.
  //
  // The options for the Qpid Proton API seems to be unsettled and there 
  // are many differences among the different versions of the code. At
  // the time of writing the most recent API version is 0.39.0. A user of the
  // AMQ network should check the documentation and ensure that the options
  // are correctly set.
  //
  // In general the default implementation provides methods returning the 
  // option classes with default options set.

public:

  class AMQProperties
  : public std::enable_shared_from_this< AMQProperties >
  {
  protected:

    virtual proton::connection_options ConnectionOptions(void) const;
    virtual proton::message::property_map MessageProperties( void ) const;
    virtual proton::message::annotation_map MessageAnnotations( void ) const;
    virtual proton::message::annotation_map MessageDelivery( void ) const;
    virtual proton::sender_options SenderOptions( void ) const;
    virtual proton::receiver_options ReceiverOptions( void ) const;

    friend class NetworkLayer;

  public:

    AMQProperties() = default;
    AMQProperties( const AMQProperties & Other ) = default;
    virtual ~AMQProperties() = default;
  };

  // The pointer to the property class is stored by the constructor and is 
  // by default initialised to the default base class above.

private:

  const std::shared_ptr< AMQProperties > Properties;

  // ---------------------------------------------------------------------------
	// Communication event handlers
	// ---------------------------------------------------------------------------
	//
  // The event handlers are listed in the order in which the will be used to 
  // establish the connection and to handle communication events. It should 
  // be noted that these are called from the event loop thread, and as such 
  // they can freely use the proton variables above. This also means that the 
  // proton variables should not be called directly from any of the Network 
  // Layer's message handlers for messages from the Session Layer.
  //
  // The first event hander indicates that the event loop has been started. 
  // When called it will simply just initialise the connection to the broker 
  // for this event loop.
  
  virtual void on_container_start( proton::container & TheLoop ) override;
  
  // The next handler is called when the connection has been established. It 
  // will simply record that the Network Layer is connected with the server.
  
  virtual void on_connection_open( proton::connection & TheBroker ) override;
  
  // When an outbound message is received and there is no sender for the 
  // topic, it will be created while the message will be cached. Then when 
  // the sender has connected, the cached messages for this topic will be 
  // queued for sending. 
  
  virtual void on_sendable( proton::sender & ThePublisher ) override;
  
  // When a local actor is created, or when a remote actor requests an actor 
  // address that is resolved to be on this node, a receiver will be created. 
  // However, it cannot be stored in the list of subscribers before it is 
  // ready, and the next event handler takes care of that.
  
  virtual void on_receiver_open(proton::receiver & TheSubscribtion ) override;
  
  // Inbound messages on any topic will call the true message handler. This 
  // will simply send the message to the Session Layer to be forwarded to 
  // the local Actor subscribing to the concerned topic.
  
  virtual void on_message( proton::delivery & DeliveryStatus, 
                           proton::message  & TheMessage ) override;
                           
  // There are three functions dealing with error handling. They are defined 
  // because they offer possibilities to get more information on the error, 
  // and if the error is deemed to be severe, a system error exception will be 
  // thrown with a descriptive message.
                           
  virtual void on_connection_error( proton::connection & TheBroker ) override;
  virtual void on_error  (const proton::error_condition & TheError ) override;

  // ---------------------------------------------------------------------------
	// Actor system sender and receiver management
	// ---------------------------------------------------------------------------
	//
  // The below handlers need a defined set of commands to be defined that can 
  // be supported among endpoints in order to signal to other actor systems 
  // that actors are removed, or to help the global address resolution. The 
  // command strings are sent as the subject of messages, and the actual actor
  // names as a string valued message body.
  
  class Protocol
  {
  public:
    
    enum class Action
    {
      ResolveAddress,
      GlobalAddress,
      ActorRemoval,
      EndpointShutDown
    };
    
    static Action      Command( const std::string & CommandText );
    static std::string String ( Action TheAction );
  };

  // When a local Actor wants to send a message to a remote Actor, it will 
  // typically only know the Actor by name and not the endpoint hosting that 
  // Actor. This because the Actor system shall be deployable on a single node 
  // as well as being distributed. For this reason a message to an unknown 
  // Actor will be delivered to the Session Layer that has the responsibility
  // to map the local actor address to a global address. This means that it will 
  // place a request on the Network Layer to check with the other Network Layers
  // where the given Actor address is hosted.   
          
protected:
  
  virtual void ResolveAddress( const ResolutionRequest & TheRequest,
														   const Address TheSessionLayer ) override;
                               
  // A resolution request from other endpoints will be forwarded to the 
  // Session Layer, and if the Session Layer detects that the requested 
  // Actor name corresponds to an actor on this endpoint, it will give a 
  // resolution response to the Network Layer that will forward the global 
  // address of the local actor on the discovery topic for all other endpoints 
  // to note. An resolution request is initiated by the endpoint having an 
  // Actor wanting to send a message to the unknown destination, but all 
  // endpoint should note the resolution and their Session Layers should 
  // record the mapping to be prepared if some actor on another endpoint also 
  // would like to message the same actor some time in the future.
                               
  virtual void ResolvedAddress( const ResolutionResponse & TheResponse,
																const Address TheSessionLayer ) override;
                                
  // A consequence of the fact that all Session Layers should cache the global
  // addresses of remote Actors, is that they also need to know when an Actor 
  // closes and will no longer be available. The local Session Layer will send 
  // a request to remove the Actor to the Network Layer that will send this 
  // information on the discovery topic for the other endpoints to note and 
  // potentially remove the sender for this actor.
                                
  virtual void ActorRemoval( const RemoveActor & TheCommand,
														 const Address TheSessionLayer ) override;

 // ---------------------------------------------------------------------------
	// Topic subscriptions
	// ---------------------------------------------------------------------------
	//
  // Subscriptions to individual topics are made by a dedicated message as 
  // the topic may not be associated with a remote endpoint and therefore there
  // is no external address to be resolved. There are two possible actions for 
  // the topic subscription: it can be opened as a subscription, it can be 
  // opened as a publisher. In both cases the topic can be closed when it is 
  // no longer needed.
                               
public:

  class TopicSubscription
  {
  public:
    
    enum class Action
    {
      Subscription,
      Publisher,
      CloseSubscription,
      ClosePublisher
    };
    
    const Action    Command;
    const TopicName TheTopic;
    
    TopicSubscription( const Action & WhatToDo, const TopicName & GivenTopic )
    : Command( WhatToDo ), TheTopic( GivenTopic )
    {}
    
    TopicSubscription( void )  = delete;
    TopicSubscription( const TopicSubscription & Other ) = default;
    ~TopicSubscription( void ) = default;
  };

protected:

  // The handler for this allows the Session Layer to set up subscriptions. 
  // The actors must subscribe to the Session layer to allow it to forward 
  // incoming messages from subscribed topics to multiple local Actors.
  
  virtual void ManageTopics( const TopicSubscription & TheMessage, 
                             const Address TheSessionLayer );
                     
  // ---------------------------------------------------------------------------
	// Message handling
	// ---------------------------------------------------------------------------
	//
  // When an actor sends a message to a remote actor a message is sent from 
  // the Session layer. The handler is checking if the publisher for the given 
  // remote receiving Actor has been created. If so, the send action is just 
  // queued. However, if the sender has not been defined, then an action is 
  // queued to create the sender and the received message is cached for 
  // later sending when the publisher is ready.

  virtual void OutboundMessage(const Theron::AMQ::Message & TheMessage, 
                               const Address TheSessionLayer) override;

  // ---------------------------------------------------------------------------
	// Starting and stopping the Network Layer
	// ---------------------------------------------------------------------------
	//
  // The constructor takes the endpoint name which will act as the actor name 
  // string. The second argument is the URL string used to connect to the 
  // broker, and the port number for the connection. The latter is given as an 
  // integer to ensure that it is a numerical value. It is also possible to 
  // give a set of connection options containing for instance the user name 
  // and password to connect to the AMQ broker, and various security related 
  // options. One can also give a property map for the messages that are sent.
  // This will be applied first, and then the 'to' and 'reply-to' fields are 
  // overwritten by the information in the outbound message from the Session
  // layer.
                     
public:
                     
  NetworkLayer( const Theron::AMQ::GlobalAddress & EndpointName,
								const std::string & BrokerURL,
							  const unsigned int & Port = 61616,
                const std::shared_ptr< AMQProperties > GivenProperties 
                      = std::make_shared< AMQProperties >() );

	// The default constructor is deleted

	NetworkLayer( void ) = delete;

  // There is a message handler to allow the network communication to be shut 
  // down in a structured manner. If the status is that the Network Layer is 
  // connected when the destructor is invoked, it will call the stop function 
  // to close connections. It will send a message on the control plane channel 
  // to indicate to all the other instances that the endpoint is closing.

protected:
  
  virtual void Stop( const Network::ShutDown & StopMessage,
										 const Address Sender ) override;

	// The destructor closes the connection and stops the AMQ connection by 
  // calling the above stop message handler directly.
                     
public:
  
	virtual ~NetworkLayer();
};

/*==============================================================================

 Default AMQ options

==============================================================================*/
//
// In order to set the default options conveniently, there is a small helper 
// function that simply returns a smart pointer to the default options class

constexpr std::shared_ptr< NetworkLayer::AMQProperties > DefaultOptions( void )
{
  return std::shared_ptr< NetworkLayer::AMQProperties >();
}


}       // End name space AMQ
#endif  // THERON_AMQ_NETWORK_LAYER
