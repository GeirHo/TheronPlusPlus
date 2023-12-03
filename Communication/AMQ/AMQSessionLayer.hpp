/*==============================================================================
Active Message Queue (AMQ): Session Layer

The generic Session Layer handles the message transport and address resolution
for actors on remote endpoints. It also handles the registration of local 
actors and the protocol for closing the network connection and the external 
availability of this endpoint, and close sessions to actors on closing remote 
endpoints.

The AMQ Session Layer adds support for the direct topic publish and subscribe
modes.

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
==============================================================================*/

#ifndef THERON_AMQ_SESSION_LAYER
#define THERON_AMQ_SESSION_LAYER

// Standard headers

#include <string>        // For standard strings
#include <unordered_map> // For O(1) lookups of topics
#include <set>           // For actors subscrigin to topics

// Theron++ Actor Framework headers

#include "Actor.hpp"
#include "Utility/StandardFallbackHandler.hpp"
#include "Communication/NetworkEndpoint.hpp"
#include "Communication/SessionLayer.hpp"

// Active Message Queue network servers

#include "Communication/AMQ/AMQMessage.hpp"
#include "Communication/AMQ/AMQNetworkLayer.hpp"

namespace Theron::AMQ
{
/*==============================================================================

 Session Layer

==============================================================================*/
//
// The session layer inherits the standard Session Layer using the AMQ 
// messsage as the external message type to be exchanged with the Network 
// Layer.

class SessionLayer
: virtual public Actor,
  virtual public StandardFallbackHandler,
	virtual public Theron::SessionLayer< AMQ::Message >
{
private:

  using GenericSessionLayer = Theron::SessionLayer< AMQ::Message >;

public:

  // --------------------------------------------------------------------------
  // Topic management
  // --------------------------------------------------------------------------
  //
  // Topics have a name, and local actors can publish to topics, or they can 
  // subscribe to topics to receive messages from the topics. The use of 
  // a topic must be explicit because otherwise the Network Layer will try to 
  // resolve the name the topic as the name of an actor on a remote endpoint. 
  // Since the topic name does not exist as an actor on any endpoint, the 
  // resolution will stay pending and the topic will not be used. Hence, local
  // actors that wants to post or receive information from a topic must 
  // make a topic subscription to the Session Layer. The format of the message
  // is identical to the format the session layer will use towards the Network
  // Layer to set up the topic senders or receivers, and so the message is 
  // just re-used for actors on this topic to send to the Session Layer.

  using TopicSubscription = NetworkLayer::TopicSubscription;

  // The topic identifier is a global address without an endpoint, and 
  // with respect to the global address format it corresponds to the actor 
  // name of inbound messages. The inbound message will be forwarded to all 
  // subscribing actors, and these are stored using the topic identifier as 
  // the key.
  // 
  // The same goes for actors publishing to topics. As there can be many local
  // actors publishing, then it is necessary to track the topics that are use
  // for publishing messages and the actors using these topics. The topic is 
  // closed when there are no more publishers.

private:

  std::unordered_map< TopicName, std::set< Address > > 
  TopicSubscribers, TopicPublishers;

protected:

  // The handler for the topic subscriptions will handle subscriptions from 
  // local actors to remote topics. 
  
  virtual void ManageTopics( const TopicSubscription & TheMessage, 
                             const Address PubSubActor );

  // --------------------------------------------------------------------------
  // Managing actors
  // --------------------------------------------------------------------------
  //
  // If an actor closes without unsubscribing from the topics inbound messages
  // will continuously be forwarded to the non-existing actor and this could 
  // cause problems. Therefore, if an actor closes without having unsubscribed
  // from any topics, it will be automatically unsubscribed. To ensure this, 
  // the handler function for remove actor message must be overloaded.
                    
  virtual void RemoveLocalActor( const RemoveActorCommand & Command,
                                 const Address ClosingActor ) override;

  // --------------------------------------------------------------------------
  // Messages on topics
  // --------------------------------------------------------------------------
  //
  // Messages arriving from remote endpoints must first be checked to see 
  // if they come from topics and if so it should just be sent to all the 
  // subscribing actors. If the message is not from a topic, it is from a 
  // remote actor and those sessions will be managed by the standard Session 
  // Layer. 

  virtual void InboundMessage( const AMQ::Message & TheMessage,
										           const Address TheNetworkLayer ) override;

  // The same consideration are used for outbound messages. If the name of the 
  // destination actor correspond with a topic name, then the message is just 
  // forwarded to the Network Layer. If the name is not known as a publisher 
  // topic, it may be a remote actor and the standard Session Layer will look 
  // up the address or initiate an address resolution request if the remote 
  // actor name is unknown.

  virtual void OutboundMessage( const InternalMessage & TheMessage,
                                const Address ThePresentationLayer ) override;

  // --------------------------------------------------------------------------
  // Constructor and destructor
  // --------------------------------------------------------------------------
  //
  // The constructor only takes the name of the endpoint and the name of the 
  // Session Layer as arguments. The default constructor and the copy 
  // constructor are removed, and the virtual destructor is just the default.

public:

  SessionLayer(const std::string & TheEndPoint, const std::string & ServerName);

  SessionLayer() = delete;
  SessionLayer( const SessionLayer & Other ) = delete;

  virtual ~SessionLayer();
};

}      // Name space Theron AMQ
#endif // THERON_AMQ_SESSION_LAYER