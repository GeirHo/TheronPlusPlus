/*==============================================================================
Active Message Queue (AMQ): Session Layer

The Session Layer adds the support for topic subscriptions and forwarding 
inbound messages from topics to the subscribing actors. 

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
==============================================================================*/

// Standard headers

#include <ranges>     // Container ranges
#include <algorithm>  // Standard algorithms

// The Theron++ headers

#include "Communication/AMQ/AMQSessionLayer.hpp"

namespace Theron::AMQ
{
/*==============================================================================

 Message handlers

==============================================================================*/
//
// ----------------------------------------------------------------------------
// Topic management
// ----------------------------------------------------------------------------
//  
// The handler allowing an actor to subscribe to messages or cancel 
// subscriptions will add the topic subscription with a message to the Network 
// Layer if this is the first time an actor subscribes to the topic. If any 
// other actor already holds a subscription, the actor is just added to the 
// set of subscribers.
// 
// The inverse happens when an actor unsubscribes, as the actor address is just
// removed if there are other actors still subscribing, and only when the last
// actor holding a subscription unsubscribes will the Network layer be 
// instructed to cancel the topic subscription from this endpoint.

void SessionLayer::ManageTopics( const TopicSubscription & TheMessage, 
                                 const Address PubSubActor )
{
  switch( TheMessage.Command )
  {
    case TopicSubscription::Action::Subscription:
      if( TopicSubscribers.contains( TheMessage.TheTopic ) )
          TopicSubscribers[ TheMessage.TheTopic ].insert( PubSubActor );
      else
      {
        TopicSubscribers.emplace( TheMessage.TheTopic, PubSubActor );
        Send( TheMessage, Network::GetAddress( Network::Layer::Network ) );
      }
      break;
    case TopicSubscription::Action::CloseSubscription:
      auto Subscribers = TopicSubscribers.find( TheMessage.TheTopic );

      if( Subscribers != TopicSubscribers.end() )
      {
        if ( Subscribers->second.size() > 1 )
          Subscribers->second.erase( PubSubActor );
        else
        {
          Send( TheMessage, Network::GetAddress( Network::Layer::Network ) );
          TopicSubscribers.erase( Subscribers );
        }
      }
      break;
    case TopicSubscription::Action::Publisher:
      if( TopicPublishers.contains( TheMessage.TheTopic ) )
        TopicPublishers[ TheMessage.TheTopic ].insert( PubSubActor );
      else
      {
        TopicPublishers.emplace( TheMessage.TheTopic, PubSubActor );
        Send( TheMessage, Network::GetAddress( Network::Layer::Network ) );
      }
      break;
    case TopicSubscription::Action::ClosePublisher:
      auto Publishers = TopicPublishers.find( TheMessage.TheTopic );

      if( Publishers != TopicPublishers.end() )
      {
        if( Publishers->second.size() > 1 )
          Publishers->second.erase( PubSubActor );
        else
        {
          Send( TheMessage, Network::GetAddress( Network::Layer::Network ) );
          TopicPublishers.erase( Publishers );
        }
      }
  };
}

// ----------------------------------------------------------------------------
// Inbound messages
// ----------------------------------------------------------------------------
//  
// An inbound message will first be checked if they come from a topic, 
// and if so the message will be sent to all subscribers. If not, it is 
// taken to be a message from a remote actor and passed on for handling by 
// the generic Session Layer.

void SessionLayer::InboundMessage( const AMQ::Message & TheMessage,
										               const Address TheNetworkLayer )
{
  if( TopicSubscribers.contains( TheMessage.GetSender().ActorName() ) )
  {
    Address SenderTopic( TheMessage.GetSender().ActorAddress() ),
            ThePresentationLayer( 
            Network::GetAddress( Network::Layer::Presentation ) );

    std::ranges::for_each( 
      TopicSubscribers[ TheMessage.GetSender().ActorName() ],
      [&,this](const Address & Subscriber){
        Send( InternalMessage( SenderTopic, Subscriber, 
                               TheMessage.GetPayload() ), 
              ThePresentationLayer ); 
    });
  }
  else
    Theron::SessionLayer< AMQ::Message >::InboundMessage( 
      TheMessage, TheNetworkLayer );
}

// ----------------------------------------------------------------------------
// Outbound messages
// ----------------------------------------------------------------------------
//  
// A check is made to see if the actor name of the destination address is a
// known publisher topic. If it is, then the message will be sent directly to 
// the Network Layer for transmission. If it is not, the message is for a 
// remote actor, and it should be handled by the generic session layer.

void SessionLayer::OutboundMessage( const InternalMessage & TheMessage,
                                    const Address ThePresentationLayer )
{
  if( TopicPublishers.contains( TheMessage.To.AsString() ) )
    Send( AMQ::Message( AMQ::GlobalAddress( TheMessage.From, EndpointName ),
                        AMQ::GlobalAddress( TheMessage.To, "" ),
                        TheMessage.MessagePayload ),
          Network::GetAddress( Network::Layer::Network ) );
  else
    Theron::SessionLayer< AMQ::Message >::OutboundMessage( 
      TheMessage, ThePresentationLayer );
}

/*==============================================================================

 Constructor and destructor

==============================================================================*/
//
// The constructor simply initialises the base classes and the map of 
// subscribed topics.

SessionLayer::SessionLayer( const std::string & TheEndPoint, 
                            const std::string & ServerName )
: Actor( GlobalAddress( ServerName, TheEndPoint ).AsString() ),
  StandardFallbackHandler( GetAddress().AsString() ),
  Theron::SessionLayer< AMQ::Message >( TheEndPoint, ServerName ),
  TopicSubscribers()
{
  RegisterHandler( this, &SessionLayer::ManageTopics );
}

} // End name space Theron AMQ