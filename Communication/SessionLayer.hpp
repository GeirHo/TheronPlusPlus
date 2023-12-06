/*=============================================================================
  Session Layer

  This actor wraps the payload received from the Presentation Layer in a
  properly coded package to be transmitted by the Network Layer server. It also
  ensures the bidirectional address transfer between the external address space
  of the network and the local actor address space so that incoming messages
  can be delivered to the right actor, and so that outgoing messages can reach
  the right actor on the right remote endpoint. Important: It is assumed that
  the actor addresses are all unique across all nodes, which means that there
  cannot be two actors on two endpoints with the same name.

  The session layer is a template on the External Message holding the two 
  unique global addresses of the two actors exchanging the message. Different
  protocols typically extends this generic message to be able to exchange 
  protocol specific messages.
  
  If an outbound packet from a local actor is targeted to an external actor 
  whose global address is unknown, an address resolution request is created 
  for this address. Further messages to the unknown remote actor will be 
  cached until the remote address has been resolved. The session layer will 
  then forward all the cached messages for the remote actor, and register the 
  known remote address so that future messages to that remote  can be sent 
  directly.

  A message received from a remote actor will carry the remote actors
  external address, which should encode the actual actor name. In this
  case, a local actor ID equal to its external address will be created in 
  the external actor map so that other local actors trying to sen to the 
  remote actor will not need to look up the address again.
 
  Author and Copyright: Geir Horn, University of Oslo
  Contact: Geir.Horn@mn.uio.no
  License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
=============================================================================*/

#ifndef THERON_SESSION_LAYER
#define THERON_SESSION_LAYER

// Standard headers

#include <set>
#include <map>
#include <unordered_map>
#include <string>
#include <utility>
#include <queue>
#include <type_traits>
#include <stdexcept>
#include <sstream>
#include <concepts>
#include <ranges>
#include <algorithm>

// Theorn++ headers

#include "Actor.hpp"
#include "Utility/StandardFallbackHandler.hpp"
#include "Utility/AddressHash.hpp"

#include "Communication/LinkMessage.hpp"
#include "Communication/NetworkEndpoint.hpp"
#include "Communication/PresentationLayer.hpp"
#include "Communication/NetworkLayer.hpp"

#include <iostream>

// The Session Layer is a part of the communication extensions for Theron
// and is therefore defined to belong to the Theron name space

namespace Theron
{
/*==============================================================================

 Session Layer Messages

==============================================================================*/
//
// The actual Session Layer is a template class on the external message format
// to be used with the compatible Network Layer class. However it is based
// on the fundamental principle that actors register with the Session Layer,
// and it is also possible to subscribe to notifications of other actors
// register. Actors using these messages should not need to know about the
// external message format used in the Session Layer template. The commands
// are therefore defined in a separate base class inherited by the external
// message specific Session Layer template.

class SessionLayerMessages
{
  // ---------------------------------------------------------------------------
  // Messages TO the Session Layer
  // ---------------------------------------------------------------------------
  // Local actors can interact with the Session Layer server by sending commands
  // to the server. There is a command to create an external address, and
  // a command to remove the external address of a local actor. Both commands
  // will be handled by message handlers.

protected:

  class RegisterActorCommand
  {
	public:

		RegisterActorCommand( void ) = default;
		RegisterActorCommand( const RegisterActorCommand & Other ) = default;
    ~RegisterActorCommand() = default;
	};

  class RemoveActorCommand
  {
	public:

		RemoveActorCommand( void ) = default;
		RemoveActorCommand( const RemoveActorCommand & Other ) = default;
    ~RemoveActorCommand() = default;
	};

	// These messages can only be sent by the de-serializing actors. The idea is
	// that in order to be able to participate to endpoint external communication
	// the actor must support serial message de-serialisation. Hence it does not
	// make sense for an actor to register unless it is derived from the de
	// de-serialising actor, and this implies that the registration can equally
	// well be done by the de-serialising actor base class' constructor, and
	// the removal of the registration from its destructor - provided that it is
	// destroyed before the session layer class!

	template< typename > friend class NetworkingActor;

  // Actors may need to know their possible peer actors, and can subscribe
  // to a notification when a (remote) new peer is discovered by sending a
  // subscription request to the Session Layer.

public:

  class NewPeerSubscription
  {
	public:

		NewPeerSubscription( void ) = default;
		NewPeerSubscription( const NewPeerSubscription & Other ) = default;
    ~NewPeerSubscription() = default;
	};

  // The inverse command simply takes the sender out of the list of subscribers

  class NewPeerUnsubscription
  {
	public:

		NewPeerUnsubscription( void ) = default;
		NewPeerUnsubscription( const NewPeerUnsubscription & Other ) = default;
    ~NewPeerUnsubscription() = default;
	};

  // ---------------------------------------------------------------------------
  // Messages FROM the Session Layer
  // ---------------------------------------------------------------------------
  // Note that an actor receiving information from the Session Layer should
  // implement handlers for these messages (or the subset that it can likely
  // expect to receive).

  // A peer subscribing will be notified by receiving a message when a new
  // peer is detected in the system and they must implement a handler for
  // the following message. It is a set of peer addresses, which is useful
  // for the first message when many peers new to the subscribing actor may be
  // available in one go. For subsequent submissions it will probably contain
  // only single peers unless a particular implementation supports adding
  // multiple peers in one go.

  class NewPeerAdded : public std::set< Address >
  {
  public:

    NewPeerAdded( const Address & ThePeer )
    : std::set< Address >{ ThePeer }
    {}
    
    // It is a little more complicated if a range of addresses is given to 
    // the constructor since the type depends on the range. The concept is 
    // taken from the exposition of the container compatible range on the page
    // https://en.cppreference.com/w/cpp/ranges/to#container_compatible_range

    #ifdef __cpp_lib_containers_ranges
    template< class RangeType >
    requires std::ranges::input_range< RangeType > &&
             std::convertible_to< std::ranges::range_reference_t< RangeType >, 
                                  Address >
    NewPeerAdded( RangeType & AddressRange )
    : std::set< Address >( AddressRange )
    {}
    #else
    template< class RangeType >
    requires std::ranges::input_range< RangeType > &&
             std::convertible_to< std::ranges::range_reference_t< RangeType >, 
                                  Address >
    NewPeerAdded( RangeType & AddressRange )
    : std::set< Address >( AddressRange.cbegin(), AddressRange.cend() )
    {}
    #endif 
  

    // The insert ranges is a C++23 feature that may not be implemented yet
    // and this provide the implementation if needed

    #ifndef __cpp_lib_containers_ranges

      template< class RangeType >
      requires std::ranges::input_range< RangeType > &&
               std::convertible_to< std::ranges::range_reference_t< RangeType >, 
                                    Address >
      void insert_range( const RangeType & AddressRange )
      { insert( AddressRange.cbegin(), AddressRange.cend() ); }

    #endif

    NewPeerAdded()  = default;
    ~NewPeerAdded() = default;
  };

  // It is also necessary to inform the subscribers in the situation where a
  // registered addresses become invalid, and a subscribing actor would need
  // to register a handler for this message.

  class PeerRemoved : public std::set< Address >
  {
  public:

    // Constructing from a single address is staight forward initialisation
  
    PeerRemoved( const Address & ThePeer )
    : std::set< Address >{ ThePeer }
    {}

    // The ranges constructor is similar to the one used for the New Peer Added
    // message above and basically requires a range with a type that is or is 
    // convertible to an actor address.

    #ifdef __cpp_lib_containers_ranges
    template< class RangeType >
    requires std::ranges::input_range< RangeType > &&
             std::convertible_to< std::ranges::range_reference_t< RangeType >, 
                                  Address >
    PeerRemoved( RangeType && AddressRange )
    : std::set< Address >( AddressRange )
    {}
    #else
    template< class RangeType >
    requires std::ranges::input_range< RangeType > &&
             std::convertible_to< std::ranges::range_reference_t< RangeType >, 
                                  Address >
    PeerRemoved( RangeType && AddressRange )
    : std::set< Address >( AddressRange.cbegin(), AddressRange.cend())
    {}    
    #endif


    PeerRemoved()  = default;
    ~PeerRemoved() = default;
  };

};

/*==============================================================================

 Session Layer

==============================================================================*/
//
// The Session Layer class is itself an actor that must be hosted by a
// Network End Point actor on the local EndPoint.
//
// The main functionality of the session layer is to map the actor names, i.e.
// the internal addresses to an external address including the network node's
// endpoint address under the given network layer protocol. It has to respond
// to resolution requests coming on the external interface from session layer
// servers on other endpoints, and it will also participate to the resolution
// of external actors' addresses.
//
// The initial implementation of the session layer used the bimap structure 
// from Boost. However this has not been updated to support useful features 
// like ranges, and this version of the session layer has been re-implemented 
// using only standard containers. The session layer maintains the following 
// structures:
// 1. Two address maps mapping from Actor addresses to global addresses
// 2. A cache of messages pending resolution of the global address of the 
//    receiving actor. When the resolution response arrives, the cached messages
//    is forwarded, and the actor gets an entry in the remote actors map,
// 3. A set of actors subscribing to the events that a new remote actor has 
//    been discovered in case they are waiting to interact with this new actor.
//
// The template argument is the message format to be used on the link and
// it must be as subclass of the extended communication's LinkMessage class for
// the class to compile, and of course the Network Layer must supply a handler
// for this type of message.

template< ValidLinkMessage ExternalMessage >
class SessionLayer 
: virtual public Actor,
  virtual public StandardFallbackHandler,
  public SessionLayerMessages
{
public:

  // The external address represents the type used to hold the address as
  // required by the protocol. The valid message concept ensures that it 
  // exist and that is derived from the global address with support for 
  // conversion to the actor address or a global address for the actor that 
  // is unique across the distributed actor system. The address type is 
  // used here so that it can be used externally

  using ExternalAddress = typename ExternalMessage::AddressType;
  
  // The messages arriving from the presentation layer must have the same 
  // payload as the payload of the external message, and so the internal 
  // messages can be defined from the presentation layer template definition.
  
  using PresentationLayerType = PresentationLayer< 
                                typename ExternalMessage::PayloadType >;
  
  using InternalMessage = PresentationLayerType::RemoteMessage;
                          
  // The expected network layer type is also defined based on the message 
                          
  using NetworkLayerType = NetworkLayer< ExternalMessage >;

  // The messages of the network layer to be reused are redeclared.

  using ResolutionRequest  = NetworkLayerType::ResolutionRequest;
  using ResolutionResponse = NetworkLayerType::ResolutionResponse;

  // ---------------------------------------------------------------------------
  // Local variables
  // ---------------------------------------------------------------------------
  //
  // Since the external address of the sending actor is created on the fly 
  // from its actor address and the endpoint identification, the latter must 
  // be stored and and used to form the external address of local actors.
  
protected:

  const std::string EndpointName;
  
  // When messages arrives from remote actors they should be forwarded to known
  // actors on this endpoint. An actor can be known in two ways: either by 
  // explicit registration, or implicitly by sending a message to a remote 
  // actor. In either case, it is added to the the local actor registry 
  // organised by the external address as the search key to be able to fast 
  // forward incoming messages.

private:
  
  std::unordered_map< ExternalAddress, Address > LocalActorRegistry;
  
  // The addresses of local actors wanting to receive notifications about new 
  // peers are kept in a simple set to avoid duplicates.
  
  std::set< Address > NewPeerSubscribers;

  // The main task of the Session Layer is to keep track of communication 
  // sessions involving a local actor and a remote actor. However, the actual
  // messages will contain the actor identifiers and it is therefore not 
  // necessary to keep track of which local actor has a session with which 
  // external actor. The purpose is therefore just to maintain the mapping 
  // from an actor address to the external address of the actor. This is 
  // done both ways: When a message is sent from a local actor to a remote 
  // actor, it does not need to consider that the actor is remote and 
  // send to the actor using its normal actor name as destination. If the 
  // actor system fails to find the actor on this endpoint, a resolution 
  // request is initiated and when it terminates with the external address 
  // unique of the remote actor. 
  
  std::unordered_map< Address, ExternalAddress > ExternalActors;
  
  // During the resolution of the external address, messages for the remote 
  // actor must be cached. It should be noted that several messages may 
  // arrive for the same external actor during the resolution process, and 
  // the cache is therefore a multi-map allowing multiple entries for the 
  // remote actor.
  
  std::unordered_multimap< Address, InternalMessage > MessageCache;

  // ---------------------------------------------------------------------------
  // Shut-down management
  // ---------------------------------------------------------------------------
	//
	// As described in the network endpoint header, a shut down message can be
	// sent to request that the network layer servers disconnect. First this
	// implies to set the flag for a connected network to false in order to
	// prevent the registration of more local actors. Then all local actors are
	// told to 'shut down'. This means that they should no longer exchange
	// messages with remote agents and disconnect. When the last local actor
	// disconnects, the session layer will send a shut down message to the
	// network layer in order to stop external communication and tell the peer
	// endpoints that this endpoint is shutting down.

	bool NetworkConnected;

protected:

	// The status of this flag can be checked by derived classes if necessary

	inline bool HasNetwork( void )
	{ return NetworkConnected; }

	// The stop handler will first prevent further messages to be sent from the
	// presentation layer and then each known local actor will be asked to
	// stop sending on the network.

	virtual void Stop( const Network::ShutDown & StopMessage,
										 const Address Sender )
	{
		Send( StopMessage, Network::GetAddress( Network::Layer::Presentation ) );

    std::ranges::for_each( std::views::values( LocalActorRegistry ),
      [&,this]( const Address & LocalActor ){
        Send( StopMessage, LocalActor ); });

		NetworkConnected = false;
	}

  // There is also possible that a remote endpoint is closing. In this case all
  // addresses held against this remote endpoint is invalidated, and should be 
  // removed. It is also necessary to inform local subscribers that all known
  // actors on the remote endpoint have been removed.

  void RemoteEndpointClosing( 
          const Network::ClosingEndpoint & ClosingMessage, 
          const Address TheNetworkLayer )
  {
    // The addresses used by local actors on this endpoint to send 
    // messages to actors on the closing endpoint is collected. 

    PeerRemoved RemovedActorAddresses( 
      std::ranges::views::keys(
        std::ranges::views::filter( ExternalActors,
          [&](const auto & ActorRecord){ 
          return ActorRecord.second.Endpoint() == ClosingMessage.Identifier; 
        }) )
    );

    // The local subscribers are informed about the removal of all the remote 
    // actors on the closing endpoint.

    std::ranges::for_each( NewPeerSubscribers, 
      [&,this](const Address & Subscriber){ 
        Send( RemovedActorAddresses, Subscriber ); });

    // Finally, all the remote address records held for the closing endpoint 
    // can be removed.

    std::ranges::for_each( RemovedActorAddresses, 
      [&,this](const Address & RemovedActor){ 
        ExternalActors.erase( RemovedActor ); });
  }

  // ---------------------------------------------------------------------------
  // Local actor registry
  // ---------------------------------------------------------------------------
  //
  // A local actor can register with the session layer to indicate that it is
  // available to receive messages from remote actors. The registration 
  // messages just makes an entry in the local actor registry map. If there 
  // are subscribers to new actor events, they will be informed. 
  // 
  // It is a philosphical question if this registration should be done 
  // even if the network has not yet been connected, but since it is about the 
  // local actor it is fine to register them as other local actors will always
  // be able to send messages to these actors as long as they remain active.

private:

  void RegisterActor( const RegisterActorCommand & Command,
                      const Address LocalActor )
	{
    LocalActorRegistry.try_emplace( 
     ExternalAddress( LocalActor, EndpointName ), LocalActor );
    
    std::ranges::for_each( NewPeerSubscribers, 
      [&,this](const Address & Subscriber){
        Send( NewPeerAdded( LocalActor ), Subscriber );
    });
	}
	
	// When a remote actor wants to send a message to an actor not on its own 
	// endpoint it initiates a resolution request. This request is received by 
	// the Network Layer that forwards a request to the session layer and if the 
	// actor has registered or if it is known by name on this node, positive 
	// response is returned, and the actor is stored as a local actor in the 
	// registry so that messages can easily be mapped for this actor.
	
	void CheckLocalActor( const NetworkLayerType::ResolutionRequest & TheRequest,
                        const Address TheNetworkLayer )
	{
    if( TheRequest.RequestedActor.IsLocalActor() )
    {
      ExternalAddress GlobalActorID( TheRequest.RequestedActor, EndpointName );
      
      LocalActorRegistry.try_emplace(GlobalActorID, TheRequest.RequestedActor);
      
      Send( typename NetworkLayerType::ResolutionResponse( 
            GlobalActorID, TheRequest.RequestingActor ),
            Network::GetAddress( Network::Layer::Network ) );
    }
	}

	// Actors can be created and destroyed at any time, and if a remote actor 
	// closes, it should be removed from the local registry and a message must 
	// be sent to the networking layer to inform other endpoints that the actor
	// is no longer available. The direct approach would be to search the local
	// actor registry by the value field for an address matching the closing 
	// actor's address. This would be linear in the number of local actors. A
	// better approach is to first form the actor's external address and check 
	// if this exists as the lookup will then be O(1), and the external address 
	// will be readily available for notifying remote endpoints.
	// 
	// If the actor is in the local registry, the Network Layer is asked 
	// to notify remote endpoints about this actor removal, and then the actor 
	// record is removed from the local registry. All local actors subscribing  
	// to the actor availability events will be notified that the actor has 
	// closed.
	// 
	// The final step of the shut down protocol is initiated when the network 
	// is marked as disconnected and the last local actor closes. Only then can 
	// the Network Layer close.
	
protected:

  virtual void RemoveLocalActor( const RemoveActorCommand & Command,
                                 const Address ClosingActor )
  {
    ExternalAddress GlobalActorID( ClosingActor, EndpointName );
    
    if( LocalActorRegistry.contains( GlobalActorID ) )
    {
			Send( typename NetworkLayerType::RemoveActor( GlobalActorID ),
				    Network::GetAddress( Network::Layer::Network ) );
      
      LocalActorRegistry.erase( GlobalActorID );
      
      if( !NetworkConnected && LocalActorRegistry.empty() )
        Send( typename Network::ShutDown(), 
              Network::GetAddress( Network::Layer::Network ) );
    }

    // Finally, all Actors on this node subscribing for peer actor events will 
    // be notified.

    std::ranges::for_each( NewPeerSubscribers, 
      [&,this](const Address & Subscriber){
        Send( PeerRemoved( ClosingActor ), Subscriber );
  });    
  }

  // ---------------------------------------------------------------------------
  // Subscriber management
  // ---------------------------------------------------------------------------
  //
  // When a peer subscribes to be notified about new peers, it will be added
  // to the set of subscribers, and it will receive a message containing the
  // peers currently known to the system. Note that addresses of the peer 
  // actors must be exported differently for the external actor and the 
  // internal actor as it it just the raw actor address that should be sent 
  // to the subscriber. It could be that no peers are known to the system, 
  // in which an empty set of addresses will be returned.

private:

  void SubscribeToPeerDiscovery( const NewPeerSubscription & Command,
                                 const Address SubscribingActor )
  {
    NewPeerAdded ExistingPeers;

    ExistingPeers.insert_range( std::views::keys( ExternalActors ) );
    ExistingPeers.insert_range( std::views::values( LocalActorRegistry ) );
    
    Send( ExistingPeers, SubscribingActor );
    NewPeerSubscribers.insert( SubscribingActor );
  }

  // The inverse function cancelling a subscription is simpler since it is 
  // just deleting the address of the subscribing actor.
  
  void UnsubscribePeerDiscovery( const NewPeerUnsubscription & Command, 
                                 const Address SubscribingActor )
  {
    NewPeerSubscribers.erase( SubscribingActor );
  }
  
  // ---------------------------------------------------------------------------
  // Outbound messages
  // ---------------------------------------------------------------------------
  //
  // Given that the session layer does not know about remote actors before
  // before they are addressed by local actors, an outbound message will be
  // cached pending the address resolution if the actor is unknown, otherwise
  // is is immediately passed on to be forwarded by the link layer.

protected:

  virtual void OutboundMessage( const InternalMessage & TheMessage,
                                const Address ThePresentationLayer )
	{
    // The external address of the local sender is constructed first
    
    ExternalAddress SenderAddress( TheMessage.From, EndpointName );
    
    // The local actor is registered if it does not already exist in the 
    // local registry
    
    LocalActorRegistry.try_emplace( SenderAddress, TheMessage.From );
    
		// The normal situation is that the external address is already known 
    // and the external message can be constructed based on the looked up 
    // external address and the global address of the sending actor.
    
    if( ExternalActors.contains( TheMessage.To ) )
    {
      Send( ExternalMessage( SenderAddress, 
                             ExternalActors[ TheMessage.To ], 
                             TheMessage.MessagePayload ),
            Network::GetAddress( Network::Layer::Network ) );
    }
    else
    {
      // The global address of the remote receiver is not known and a resolution
      // request is initiated before the message is cached using the receiver 
      // actor's address as key.
      
      Send( typename NetworkLayerType::ResolutionRequest(
            TheMessage.To, SenderAddress ),
            Network::GetAddress( Network::Layer::Network ) );
      
      // Then caching the message waiting for the address to be resolved some 
      // time in the future. Note that this may not be immediate because the 
      // remote receiver may not yet exist since actors can be created or 
      // destroyed dynamically.
      
      MessageCache.emplace( TheMessage.To, TheMessage );
    }    
	}
  
  // When the resolved address comes back the message will provide both the 
  // address of the searched remote actor, and the global address of the 
  // local requesting actor. It will simply store the global address of the 
  // remote actor, and forward any cached messages. 

private:

  void StoreExternalAddress(
		   const NetworkLayerType::ResolutionResponse & AddressRecord,
			 const Address TheNetworkLayer	)
  {
    // The actor address of the remote actor is stored first.
    
    Address RemoteActor( AddressRecord.RequestedActor.ActorAddress() );
    
    // The actor addresses are stored if they do not exist in the list of 
    // external actors.
    
    ExternalActors.try_emplace( RemoteActor, AddressRecord.RequestedActor );
    
    // if there are messages cached for this remote actor, they should be 
    // forwarded and deleted from the cache.
    
    if( MessageCache.contains( RemoteActor ) )
    {
      auto [First, Last] = MessageCache.equal_range( RemoteActor );
      
      for( auto & [_, TheMessage] : std::ranges::subrange( First, Last ) )
        Send( ExternalMessage( ExternalAddress( TheMessage.From, 
                                                EndpointName ),
                               AddressRecord.RequestedActor,
                               TheMessage.MessagePayload ),
              Network::GetAddress( Network::Layer::Network ) );
              
      MessageCache.erase( First, Last );
    }
	}

  // In the same way, the session layer must also respond to requests to remove
  // remote actors, and it should also notify peer subscribers. If the actor
  // is not known, then nothing will happen.

  void RemoveRemoteActor( const NetworkLayerType::RemoveActor & Command, 
                          const Address TheNetworkLayer )
	{
		auto AddressRecord = ExternalActors.find( 
                                        Command.GlobalAddress.ActorAddress() );

		if ( AddressRecord != ExternalActors.end() )
		{
			// All subscribers are informed about this removal first

      std::ranges::for_each( NewPeerSubscribers, 
        [&,this]( const Address & Subscriber ){
          Send( PeerRemoved( AddressRecord->second.ActorAddress() ), 
                Subscriber );
        });

			// Any messages cached for this remote actor will simply be deleted.
			// hence the sending actor should be robust and aware that a message
			// may not arrive if the actors are volatile.

			auto [First, Last] = MessageCache.equal_range( 
                                        AddressRecord->second.ActorAddress() );
			MessageCache.erase( First, Last );

			// Finally, the address record can be deleted.

			ExternalActors.erase( AddressRecord );
		}
	}
	
  // --------------------------------------------------------------------------
  // Inbound messages
  // --------------------------------------------------------------------------
  //
  // If a message does not have a local receiving actor, it will simply be
  // ignored. This is necessary in the case where the local actor shuts down
  // at the same time as a remote actor sends a message. The information that
  // the actor is closing may cross with the message from the remote sender, and
  // the message cannot be delivered. In order to ensure the delivery in a
  // distributed setting an acknowledgement protocol should be implemented
  // among the actors, but this is application dependent.
  //
  // Inbound messages from unregistered senders is perfectly possible, but 
  // creates a philosophical problem. It could be that these senders have not 
  // been registered by the remote endpoint because they are not supposed 
  // to receive any messages. Remember that the registration is done by the 
  // Networking Actor, and it is only necessary to inherit the Networking
  // Actor if the actor is receiving messages from actors on remote endpoints.
  // In theory, an actor can send messages without being able to receive 
  // message. If the actors implement a publish-subscribe pattern, only the 
  // subscribing actors will receive messages, and the pattern is not 
  // symmetric. The endpoint hosting the subscribing actor may thus not 
  // know about the sender of the message, and it has to set the actor 
  // address of the sender to a string representation of the external 
  // address. The question is should it also register this sender as a 
  // known actor?
  //
  // The risk is that storing the sender in a situation where remote sender
  // actors are frequently created and destroyed will cause the known actor
  // registry to grow very large. These automatically registered actors will
  // never be removed since they are not explicitly registered by the remote
  // endpoint, and therefore when the remote actor closes, its external address
  // is not removed from peer endpoints by the session layer on the node
  // hosting the sender.
  //
  // It is therefore taken that remote senders that should be stored as known
  // actors must be explicitly registered by the network layer protocol using
  // messages to the session layer's store external address function. The
  // drawback is when the local receiver actor responds to an incoming message
  // from an un-registered remote sender. In this case, the outbound message
  // will be cached while the external address of the remote actor is being
  // resolved, and the cached messages will only be forwarded once the network
  // layer protocol has stored the external address of the remote actor.
  //
  // The handler is virtual in case derived technology specific session layers
  // may want to add additional processing, and the remote message of the
  // presentation layer is made available for derived classes for the same
  // reason.

protected:

  virtual void InboundMessage( const ExternalMessage & TheMessage,
										           const Address TheNetworkLayer )
	{
    if( LocalActorRegistry.contains( TheMessage.GetRecipient() ) )
			Send( InternalMessage( TheMessage.GetSender().ActorAddress(), 
                             TheMessage.GetRecipient().ActorAddress(), 
                             TheMessage.GetPayload()  ),
						Network::GetAddress( Network::Layer::Presentation ) );
	}

  // --------------------------------------------------------------------------
  // Constructor and destructor
  // --------------------------------------------------------------------------
  //
  // The constructor only takes the name of the endpoint and the name of the 
  // Session Layer as arguments.

public:

  SessionLayer(const std::string & TheEndPoint, const std::string & ServerName)
  : Actor( ServerName ),
    StandardFallbackHandler( Actor::GetAddress().AsString() ),
    SessionLayerMessages(),
    EndpointName( TheEndPoint ), LocalActorRegistry(), NewPeerSubscribers(),
    ExternalActors(), MessageCache(),
		NetworkConnected( true )
  {
    RegisterHandler( this, &SessionLayer<ExternalMessage>::Stop                     );
    RegisterHandler( this, &SessionLayer<ExternalMessage>::RemoteEndpointClosing    );
		RegisterHandler( this, &SessionLayer<ExternalMessage>::RegisterActor            );
    RegisterHandler( this, &SessionLayer<ExternalMessage>::CheckLocalActor    	    );
		RegisterHandler( this, &SessionLayer<ExternalMessage>::RemoveLocalActor    	    );
    RegisterHandler( this, &SessionLayer<ExternalMessage>::SubscribeToPeerDiscovery );
    RegisterHandler( this, &SessionLayer<ExternalMessage>::UnsubscribePeerDiscovery );
		RegisterHandler( this, &SessionLayer<ExternalMessage>::OutboundMessage    	    );
    RegisterHandler( this, &SessionLayer<ExternalMessage>::StoreExternalAddress	    );
		RegisterHandler( this, &SessionLayer<ExternalMessage>::RemoveRemoteActor        );
    RegisterHandler( this, &SessionLayer<ExternalMessage>::InboundMessage           );
  }

  // The compatibility constructor simply delegates to the normal constructor,
  // and forgets about the given Framework pointer.

  SessionLayer( Framework * HostPointer,
								const std::string & ServerName, const std::string TheEndPoint )
	: SessionLayer( TheEndPoint, ServerName )
	{ }

  // The destructor has nothing to do, but is a place holder for derived classes

  virtual ~SessionLayer()
  {};
};

}	// End name space Theron
#endif 	// THERON_SESSION_LAYER
