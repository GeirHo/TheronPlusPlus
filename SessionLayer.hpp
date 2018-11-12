/*=============================================================================
  Session Layer

  This actor wraps the payload received from the Presentation Layer in a 
  properly coded package to be transmitted by the Network Layer server. It also 
  ensures the bidirectional address transfer between the external address space 
  of the network and the local actor address space so that incoming messages 
  can be delivered to the right actor, and so that outgoing messages can reach 
  the right actor on the right remote EndPoint. Important: It is assumed that 
  the actor addresses are all unique across all nodes, which means that there 
  cannot be two actors on two EndPoints with the same name.
  
  The actor is a template on the ExternalAddress to hold the network wide 
  address of an actor. It creates a link message consisting of the external 
  sender address, the receiver address and the protocol conforming datagram. 
  Different protocols can be implemented by overloading the datagram encoder 
  function that maps a serialised message to an ExternalMessage.
  
  If a packet is targeted to an external actor whose address is unknown, an 
  address resolver actor is created for this address. Further messages to the 
  unknown remote actor will be queued by the resolver actor until it has 
  resolved the remote address. It will then forward all the enqueued messages 
  and register the known remote address with the protocol engine so that future
  messages to that actor can be sent directly.

  A message received from a remote actor will carry the remote actors 
  external address, but the actual actor ID will remain unknown. In this 
  case, a local actor ID equal to its external address will be created. 
  However, it could still be that another local actor on this endpoint does 
  know the remote actor's ID and will later send a message to this actor by 
  its real ID. In this case an address resolution will occur, and the external 
  address of the remote actor ID will be returned. We will then try to 
  remember two mappings for the same external address: to the temporary 
  assigned actor ID and to the real actor ID. 
  
  This is a problem if the local actors are remembering the IDs of the 
  remote actors they receive messages from. In this case some local actors 
  my remember the temporary address and some actors my remember the real 
  address. In request-response situations the response message should be 
  delivered to the request actor coming from the same actor as it sent the 
  request to, thus, if an actor sent a request to a temporary address, it 
  should also receive messages as if they are always from this temporary 
  address. However, if we know the real ID, it should be used for incoming 
  messages from this remote actor, and hence the requesting actor may receive 
  the response from an unknown sender using the real actor ID. For this reason,
  we need to resolve the real ID of a remote actor sending a message to an
  actor on this endpoint on the first message received from that actor.
  
  Author: Geir Horn, University of Oslo, 2015-2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef THERON_SESSION_LAYER
#define THERON_SESSION_LAYER

// Included headers

#include <set>
#include <map>
#include <unordered_map>
#include <string>
#include <utility>
#include <queue>
#include <type_traits>
#include <stdexcept>
#include <sstream>

#include <boost/bimap/bimap.hpp>
#include <boost/bimap/set_of.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <boost/bimap/tags/tagged.hpp>

#include "Actor.hpp"
#include "StandardFallbackHandler.hpp"
#include "AddressHash.hpp"

#include "LinkMessage.hpp"
#include "NetworkEndPoint.hpp"
#include "PresentationLayer.hpp"
#include "NetworkLayer.hpp"

#include <iostream>

// The Session Layer is a part of the communication extensions for Theron 
// and is therefore defined to belong to the Theron name space

namespace Theron
{
/*==============================================================================

 Session Layer Messages

==============================================================================*/
//
// The actual Session Sayer is a template class on the external message format
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
	};
  
  class RemoveActorCommand
  {
	public:
		
		RemoveActorCommand( void ) = default;
		RemoveActorCommand( const RemoveActorCommand & Other ) = default;
	};
  
	// These messages can only be sent by the Deserialising actors. The idea is 
	// that in order to be able to participate to endpoint external communication
	// the actor must support serial message de-serialisation. Hence it does not 
	// make sense for an actor to register unless it is derived from the de 
	// de-serialising actor, and this implies that the registration can equally 
	// well be done by the de-serialising actor base class' constructor, and 
	// the removal of the registration from its destructor.
	
	friend class DeserializingActor;
	
  // Actors may need to know their possible peer actors, and can subscribe 
  // to a notification when a new peer is discovered by sending a subscription
  // request to the Session Layer. 

public: 
	
  class NewPeerSubscription
  { 
	public:
		
		NewPeerSubscription( void ) = default;
		NewPeerSubscription( const NewPeerSubscription & Other ) = default;
	};

  // The inverse command simply takes the sender out of the list of subscribers 
  
  class NewPeerUnsubscription
  { 
	public:
		
		NewPeerUnsubscription( void ) = default;
		NewPeerUnsubscription( const NewPeerUnsubscription & Other ) = default;
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
    
    NewPeerAdded( void )
    : std::set< Address >()
    { }
    
    NewPeerAdded( const Address & ThePeer )
    : std::set< Address >()
    { 
      insert( ThePeer ); 
    }    
  };

  // It is also necessary to inform the subscribers in the situation where a 
  // registered address becomes invalid, and a subscribing actor would need 
  // to register a handler for this message.

  class PeerRemoved
  {
  private:
    
    Address RemovedPeer;
    
  public:
    
    PeerRemoved( const Address & ThePeer )
    : RemovedPeer( ThePeer )
    { }
    
    Address GetAddress( void ) const
    { return RemovedPeer; }
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
// In order to fulfil this core task it has 
//
// A. A bidirectional address mapping between external actor addresses and 
//    raw actor addresses. This stores the external addresses of the local 
//    actors that may be requested from remote session layer servers.
// B. An internal protocol: Messages and message handlers to accept 
//    registration from local actors and de-registrations when the local 
//    actor disappears.
// C. An external protocol: for resolving network endpoints with peer session 
//    layer servers.
// D. A command protocol: used to interface the local link layer server and 
//    the local presentation layer server.
//
// All of these parts are described and elaborated in the following.
//
// The template argument is the message format to be used on the link and
// it must be as subclass of the extended communication's LinkMessage class for 
// the class to compile, and of course the Network Layer must supply a handler
// for this type of message.
  
template < class ExternalMessage >
class SessionLayer : virtual public Actor, 
										 virtual public StandardFallbackHandler,
										 public SessionLayerMessages
{
public:
  
  // The main functionality of the Session Layer is to encode an external 
  // message that can be sent to the Network Layer for external transmission; or 
  // handle a external message delivered from the Network Layer. The external 
  // message consists of the remote addresses of the sender and recipient, 
  // and the datagram. The external message must confirm to the Network Layer
  // interface, i.e. it must be derived from that class, and this condition
  // is checked before we continue the compilation.
  
  static_assert(  std::is_base_of< 
    LinkMessage< typename ExternalMessage::AddressType >, 
    ExternalMessage >::value,
    "Session Layer: External message must be derived from Link Message" );

  // The external address represents the type used to hold the address as 
  // required by the protocol. In the case of IP addresses, this is typically 
  // a string, but it could be an integer or any other binary form.
  // The address type is defined so that it can be used externally
  
  using ExternalAddress = typename ExternalMessage::AddressType;
	
	// The message type is also defined for standard reference
	
	using MessageType = ExternalMessage;
  
  // ---------------------------------------------------------------------------
  // Address map
  // ---------------------------------------------------------------------------
  // 
  // Each actor has two addresses, one external useful for the communication 
  // protocol, and one internal actor address used by Theron to forward packets.
  // The latter will in this context be called the actor's ID and the former is
  // the external address.
  //
  // The IDs are essentially local for the actors attached to an endpoint. Only 
  // if a local ID cannot be found should the message be sent externally. This 
  // means that a local actor will overshadow a remote actor with the same ID. 
  // The consequence is, however, that actors expecting responses to their 
  // messages should have unique IDs since the ID is the only thing that is 
  // sent to the local actor together with the message. If the local actor 
  // responds, the message would not go to the remote ID but to the local actor 
  // if there is a local actor with the same ID as the remote sender.
  //
  // An actor can be created in two ways: With an explicit name string, like 
  // the following "MyActorName", or with an automatically assigned ID. In 
  // latter case, the Framework and the EndPoint will ensure that the assigned 
  // ID is unique as it will be bound to the EndPoint. However, if it is 
  // manually given there is no way we can assure uniqueness and prevent the 
  // overshadowing effect.
  //
  // It is therefore sufficient to have one map between the external address 
  // and the actor ID irrespective of the location of the actor (local or 
  // remote). However, it should be possible to look up actors both based on 
  // their IDs and based on their external addresses. This calls for a bi-
  // directional map, and the boost::bimap provides this functionality. 
  //
  // It would be great if the tagged version of the bimap could be used,
  // but this does not work with template classes, see question asked at
  // http://stackoverflow.com/questions/30704621/tagged-boostbimap-in-templates-do-they-work
  // 
  // The bi-map is then defined for the two types given by the external 
  // address as the left hand side and the actor ID represented by the 
  // Theron::Address object as the right hand side. 

protected: 

  using ActorRegistryMap = boost::bimaps::bimap <
    boost::bimaps::unordered_set_of< ExternalAddress >, 
    boost::bimaps::set_of< Address > 
  >;
  
  using ActorRecord = typename ActorRegistryMap::value_type;
  
  // Then the actual map will be an instance of the actor registry map using
  // the actor record to create new actor records when the information becomes 
  // available.
	//
	// IMPORTANT: It should be noted that the underlying model is "lazy" meaning 
	// that only local actors are stored, and the addresses of remote actors 
	// that receive messages from actors on this network endpoint. This implies 
	// that all endpoints must receive information when an actor de-register, 
	// but there is no need to ensure consistency of actor addresses when the 
	// actor system endpoint starts.
	//
	// The map of known actors is only protected to allow technology specific 
	// extensions of the session layer class.
  
  ActorRegistryMap KnownActors;
	
	// A consequence of this lazy approach is that it is necessary to cache 
	// messages to unknown recipients until their external addresses have been 
	// resolved. Since there can be many messages for one receiver, a multi-map
	// is used 
	
private:
	
	std::unordered_multimap< Address, ExternalMessage > MessageCache;

  // --------------------------------------------------------------------------
  // New peer notification subscriptions
  // --------------------------------------------------------------------------
  //
  // Other local actors may want to know when actors, remote or local, become
  // available and known. For this they can set up a subscription and receive
  // the known actor addresses.
  //
  // The set of active subscribers are kept in a set of subscribers to avoid 
  // double subscriptions and ensure that only one subscription is stored for 
  // each actor.
  
  std::set< Address > NewPeerSubscribers;
   
  // When a peer subscribes to be notified about new peers, it will be added 
  // to the set of subscribers, and it will receive a message containing the 
  // peers currently known to the system. It could be that no peers are known
  // to the system, in which an empty set of addresses will be returned.
  
  void SubscribeToPeerDiscovery( 
    const SessionLayerMessages::NewPeerSubscription & Command, 
    const Address RequestingActor )
  {
    NewPeerSubscribers.insert( RequestingActor );
    NewPeerAdded ExistingPeers;

    for ( auto Peer  = KnownActors.right.begin();
				       Peer != KnownActors.right.end(); ++Peer )
		  ExistingPeers.insert( Peer->first );
		
    Send( ExistingPeers, RequestingActor );
  }
  
  void UnsubscribePeerDiscovery( 
    const SessionLayerMessages::NewPeerUnsubscription & Command, 
    const Address RequestingActor )
  {
    NewPeerSubscribers.erase( RequestingActor );
  }

  // --------------------------------------------------------------------------
  // Local actor registration and de-registration
  // --------------------------------------------------------------------------
  //
  // When a local de-serialising actor starts it may register if it may receive
  // messages from actors on remote endpoints. This registration will add the 
  // address to the known actors, and send the actor's address to the new 
  // peer notification subscribers. 
  //
  // There is a hatch: The bi-map of know actors requires both the external 
  // address and the local actor address. The external address must be resolved
  // by the network layer. However, it is unknown how long this process will 
  // take. In the meantime a void external address could be used, but if a 
  // second actor registers before the resolution for the first actor is 
  // complete, it will receive the same default external address. However, the 
  // bi-map will not accept two identical external addresses and the second 
  // registration will be refused. Since the actual format of the external 
  // address is technology dependent, it is not possible to construct a 
  // realistic external address unique to the actor being registered (some kind
  // of actor address hash) in a default way. 
  //
  // Even if this may overcome the problem of registering the actor in the 
  // bi-map, there is a risk that this actor will actually follow the 
  // registration by sending an external message, and then will be identified 
  // with an external address that cannot be used to route the incoming message
  // if the external address has been resolved to something else in the 
  // meantime. The only viable solution is to make an incomplete registration 
  // by just recording the registration, and then finalise it once the response
  // from the network layer arrives. Messages sent from this local actor will 
  // just be cached until the resolution response arrives.
  // 
  // If some actor subscribes to the registered peers, the subscribing actors 
  // will not be informed about this new registration before its external 
  // address has been resolved. The registration handler will therefore simply 
  // create a resolution request for the actor.
  
  void RegisterActor( const SessionLayerMessages::RegisterActorCommand & Command, 
							        const Address LocalActor )
	{		
			Send( 
			typename NetworkLayer< ExternalMessage >::ResolutionRequest( LocalActor ),
			Network::GetAddress( Network::Layer::Network	) 	);
	}
  
  // When the resolved address comes back it will be associated with the 
  // already recorded actor address. Note that this can also be used to update
  // a previously stored address if the link protocol can change the address 
  // autonomously. It can also be that this is a response to a request to store 
  // the address of a remote actor whose global address has been resolved. 
  //
  // Any cached messages must be forwarded if the external address was a new 
  // external address and there are cached outbound messages for this remote 
  // actor
  
  void StoreExternalAddress( 
		   const typename 
		   NetworkLayer< ExternalMessage >::ResolutionResponse & AddressRecord, 
			 const Address TheNetworkLayer	)
  {
		auto ByID = KnownActors.right.find( AddressRecord.TheActor );
		
		if ( ByID != KnownActors.right.end() )
			KnownActors.right.replace_data( ByID, AddressRecord.GlobalAddress );
		else
		{
			// This is a response to a resolution request created when a local 
			// actor wanting to send a message to this remote actor and both 
			// the external address and the local address should be stored
			
			KnownActors.insert( ActorRecord( AddressRecord.GlobalAddress, 
																			 AddressRecord.TheActor ) );

			// If there are subscribers that should be informed about the new actor 
			// registration, they should all be informed about this event.
			
			if ( ! NewPeerSubscribers.empty() )
		  {
				NewPeerAdded NewPeer;
				NewPeer.insert( AddressRecord.TheActor );
				
				for ( auto & AnActor : NewPeerSubscribers )
					Send( NewPeer, AnActor );				
			}
			
			// Then it must be checked if there are any cached messages for this 
			// remote receiver.
			
			auto Range = MessageCache.equal_range( AddressRecord.TheActor );
			
			// Then the external messages cached for this receiver will be sent 
			// to the network layer server
			
			Address TheNetworkLayer( Network::GetAddress( 
														   Network::Layer::Network ) );
			auto Message = Range.first;
			
			while ( Message != Range.second )
		  {
				Send( ExternalMessage( Message->second.GetSender(), 
															 AddressRecord.GlobalAddress, 
															 Message->second.GetPayload() ), 
							TheNetworkLayer );
				MessageCache.erase( Message++ );
			}
		}
	}
	
  // In the same way there is a command handler for removing the external 
  // address of an actor (local or external). There can also be protocol 
  // specific side effects for this action, however the default behaviour is 
  // just to look up the actor in the map and remove it if it has a 
  // registration. The information that the actor has been removed is also 
  // passed back to all subscribers of peer actor information.

  void RemoveLocalActor( 
									  const SessionLayerMessages::RemoveActorCommand & Command,
						        const Address ActorAddress )
  {
    auto AddressRecord = KnownActors.right.find( ActorAddress );

		if (AddressRecord != KnownActors.right.end() )
    {
			// First the network layer is asked to remove remote references for this 
			// actor to prevent remote actors from sending further messages.
			
			Send( typename NetworkLayer<ExternalMessage>::RemoveActor( 
										 AddressRecord->second ), 
				    Network::GetAddress( Network::Layer::Network ) ); 
			
			// The actor is then forgotten locally
			
      KnownActors.right.erase( AddressRecord );
			
			// Finally all local subscribers can be informed about this event.
 			
      for ( const Address & Subscriber : NewPeerSubscribers )
				Send( PeerRemoved( ActorAddress ), Subscriber );
    }
  }
  
  // In the same way, the session layer must also respond to requests to remove
  // remote actors, and it should also notify peer subscribers. If the actor 
  // is not known, then nothing will happen.
  
  void RemoveRemoteActor( 
				  const typename NetworkLayer< ExternalMessage >::RemoveActor & Command, 
					const Address TheNetworkLayer )
	{
		auto AddressRecord = KnownActors.left.find( Command.GlobalAddress );
		
		if ( AddressRecord != KnownActors.left.end() )
		{
			// All subscribers are informed about this removal first
			
			for ( const Address & Subscriber : NewPeerSubscribers )
				Send( PeerRemoved( AddressRecord->second ), Subscriber );
			
			// Any messages cached for this remote actor will simply be deleted. 
			// hence the sending actor should be robust and aware that a message 
			// may not arrive if the actors are volatile.
			
			auto Result  = MessageCache.equal_range( AddressRecord->second );
			auto Message = Result.first;
			
			while ( Message != Result.second )
				MessageCache.erase( Message++ );
			
			// Finally, the address record can be deleted.
			
			KnownActors.left.erase( AddressRecord );
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
  // the message cannot be delivered.In order to ensure the delivery in a 
  // distributed setting an acknowledgement protocol should be implemented 
  // among the actors.
  //
  // Inbound messages from registered senders is perfectly possible, but creates 
  // a philosophical problem. It could be that these senders have not been 
  // registered by the remote endpoint because they are not supposed to receive
  // any messages. Remember that the registration is done by the Deserializing
  // actor, and it is only necessary to inherit the Deserializing actor if the 
  // actor is receiving messages from actors on remote endpoints. If the actors
  // implement a publish-subscribe pattern, only the subscribing actors will 
  // receive messages, and the pattern is not symmetric. The endpoint hosting 
  // the subscribing actor may thus not know about the sender of the message, 
  // and it has to set the actor address of the sender to a string 
  // representation of the external address. The question is should it also 
  // register this sender as a known actor?
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
  
  void InboundMessage( const ExternalMessage & TheMessage, 
											 const Address TheNetworkLayer )
	{
		auto ReceiverRecord = KnownActors.left.find( TheMessage.GetRecipient() );
		
		if ( ReceiverRecord != KnownActors.left.end() )
		{
			// The actor address of the sender must be set for the message to be 
			// valid. It can either be stored in the actor registry if the remote 
			// actor has already been registered, or it can be set to a temporary 
			// remote actor address.
			
			Address RemoteActor;
		
			// The sender's actor address is resolved if it is registered.
			
			auto SenderRecord = KnownActors.left.find( TheMessage.GetSender() );
			
			if ( SenderRecord != KnownActors.left.end() )
				RemoteActor = SenderRecord->second;
			else
				RemoteActor = TheMessage.ActorAddress( TheMessage.GetSender() );
			
			// The sender's actor address is now valid, and the message can be 
			// forwarded to the presentation layer to be de-serialised.
			
			Send( PresentationLayer::RemoteMessage( RemoteActor, 
																							ReceiverRecord->second, 
																					    TheMessage.GetPayload()  ), 
						Network::GetAddress( Network::Layer::Presentation ) 
					);
		}
	}
  
  // --------------------------------------------------------------------------
  // Outbound messages
  // --------------------------------------------------------------------------
  // 
  // Given that the session layer does not know about remote actors before 
  // before they are addressed by local actors, an outbound message will be 
  // cached pending the address resolution if the actor is unknown, otherwise 
  // is is immediately passed on to be forwarded by the link layer.
  
  void OutboundMessage( const PresentationLayer::RemoteMessage & TheMessage,
												const Address ThePresentationLayer )
	{
		// The receiver and the senders are looked up by their actor addresses
		
		auto TheReceiver = KnownActors.right.find( TheMessage.GetReceiver() ),
		     TheSender   = KnownActors.right.find( TheMessage.GetSender() );

		// It is a philosophically difficult problem if the sender does not 
		// exists as a known actor since this means that the actor has not 
		// registered, and the Deserializing Actor base class will register all 
		// derived actors. In other words, the actor is not able to receive 
		// serialised messages, and no message should be routed back to it. 
		// Yet, it is probably correct to send it, but with an empty return 
		// address. Hence, if the lookup fails it is assumed that the sender 
	  // address is initialised to something that has a default value by the 
	  // default constructor of the External message, and the sender field is 
	  // not set.

	  ExternalAddress SenderAddress;
		
		if ( TheSender != KnownActors.right.end() )
			SenderAddress = TheSender->second;
			
		// Then the datagram can be sent or queued depending on whether the 
		// external address of the receiver is already known or not. If it is 
		// known then message can just be forwarded to the Network Layer server 
		// for handling.
		
		if ( TheReceiver != KnownActors.right.end() )
			Send( ExternalMessage( SenderAddress, TheReceiver->second, 
														 TheMessage.GetPayload() ),	
						Network::GetAddress( Network::Layer::Network ) );
		else
		{
			// The external address of the receiver is currently not known, and it 
			// should be requested from the network layer - but only if there is 
			// not already a pending request indicated by a cached message.
			
			if ( MessageCache.find( TheMessage.GetReceiver() ) == MessageCache.end() )
				Send( typename NetworkLayer< ExternalMessage >::ResolutionRequest( 
							  TheMessage.GetReceiver() ),
				      Network::GetAddress( Network::Layer::Network	)	);
			
			// Then the message is cached for delayed sending once the response comes
			// back from the network layer. In this case the receiver field is left 
		  // uninitialised until the resolution response comes back from the 
		  // Network Layer server.
			
			MessageCache.emplace( TheMessage.GetReceiver(), 
														ExternalMessage( SenderAddress, ExternalAddress(), 
																						 TheMessage.GetPayload() ) );
		}
	}

  // --------------------------------------------------------------------------
  // Constructor and destructor
  // --------------------------------------------------------------------------
  //
  // The constructor then takes only the name of the Session Layer as 
  // argument after the mandatory pointer to the network endpoint. However, 
  // note that there is a default initialisation of the addresses of the 
  // Network Layer server and the Presentation Layer server. This is possible 
  // since a Theron Address  object initialised with a string will not check if 
  // this corresponds to a legal address before a message is being sent to this 
  // address. Hence, if the default names are used, then the explicit binding 
  // of their addresses is not necessary.

public:
    
  SessionLayer( const std::string & ServerName = "SessionLayer"  )
  : Actor( ServerName ), 
    StandardFallbackHandler( ServerName ),
    SessionLayerMessages(), 
    KnownActors(), MessageCache(),NewPeerSubscribers()
  { 
    RegisterHandler( this, &SessionLayer<ExternalMessage>::SubscribeToPeerDiscovery );
    RegisterHandler( this, &SessionLayer<ExternalMessage>::UnsubscribePeerDiscovery );
		RegisterHandler( this, &SessionLayer<ExternalMessage>::RegisterActor            );
		RegisterHandler( this, &SessionLayer<ExternalMessage>::StoreExternalAddress	    );
		RegisterHandler( this, &SessionLayer<ExternalMessage>::RemoveLocalActor    	    );
    RegisterHandler( this, &SessionLayer<ExternalMessage>::RemoveRemoteActor        );
    RegisterHandler( this, &SessionLayer<ExternalMessage>::OutboundMessage    	    );
    RegisterHandler( this, &SessionLayer<ExternalMessage>::InboundMessage           );
  }

  // The compatibility constructor simply delegates to the normal constructor,
  // and forgets about the given Framework pointer.
  
  SessionLayer( Framework * HostPointer, 
								const std::string & ServerName = "SessionLayer" )
	: SessionLayer( ServerName )
	{ }
    
  // The destructor has nothing to do, but is a place holder for derived classes
  
  virtual ~SessionLayer()
  { }
};
  
} 	// End name space Theron
#endif 	// THERON_SESSION_LAYER
