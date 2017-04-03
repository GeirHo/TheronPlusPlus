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

#include <Theron/Theron.h>
#include "StandardFallbackHandler.hpp"

#include "LinkMessage.hpp"
#include "NetworkEndPoint.hpp"
#include "PresentationLayer.hpp"
#include "NetworkLayer.hpp"

#include <iostream>

// The Session Layer is a part of the communication extensions for Theron 
// and is therefore defined to belong to the Theron name space

namespace Theron
{
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
public:
  
  // ---------------------------------------------------------------------------
  // Messages TO the Session Layer
  // ---------------------------------------------------------------------------
  // Local actors can interact with the Session Layer server by sending commands
  // to the server. There is a command to create an external address, and 
  // a command to remove the external address of a local actor. Both commands
  // will be handled by message handlers. The address of the actor concerned 
  // could have been passed in the class, but since the handler receives the 
  // address of the local actor as sender, it is not needed to make a memory 
  // copy of this address. 

  class RegisterActorCommand
  { 
	private:
		
		std::string Description;
		
	public:
		
		RegisterActorCommand( void )
		{
			Description = "Session Layer: Register Actor Command";
		}
		
	};
  
  class RemoveActorCommand
  {
	private:
		
		std::string Description;
		
	public:
		
		RemoveActorCommand( void )
		{
			Description = "Session Layer: Remove Actor Command";
		}
	};
  
  // Actors may need to know their possible peer actors, and can subscribe 
  // to a notification when a new peer is discovered by sending a subscription
  // request to the Session Layer. 

  class NewPeerSubscription
  { 
	private:
		
		std::string Description;
		
	public:
		
		NewPeerSubscription( void )
		{
			Description = "Session Layer: New Peer Subscription";
		}
	};

  // Unsubscribe requests simply removes the peer from the subscriber set 
  
  class NewPeerUnsubscription
  { 
	private:
		
		std::string Description;
		
	public:
		
		NewPeerUnsubscription( void )
		{
			Description = "Session Layer: New Peer UnSubscription"; 
		}
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

  
// The Session Layer class is itself an actor that must be hosted by a 
// framework on the local EndPoint. 
//
// The template argument is the message format to be used on the link and
// it must be as subclass of the extended communication's LinkMessage class for 
// the class to compile, and of course the Network Layer must supply a handler
// for this type of message.
  
template < class ExternalMessage >
class SessionLayer : public virtual Actor, 
										 public virtual StandardFallbackHandler,
										 public SessionLayerMessages
{
public:
  
  // The external address represents the type used to hold the address as 
  // required by the protocol. In the case of IP addresses, this is typically 
  // a string, but it could be an integer or any other binary form.
  // The address type is defined so that it can be used externally
  
  using ExternalAddress = typename ExternalMessage::AddressType;
  
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
    "SessionLayer: External message must be derived from LinkMessage" );
  
private:

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
  // but this does not work with templated classes, see question asked at
  // http://stackoverflow.com/questions/30704621/tagged-boostbimap-in-templates-do-they-work
  // 
  // The bi-map is then defined for the two types given by the external 
  // address as the left hand side and the actor ID represented by the 
  // Theron::Address object as the right hand side. 
  
  typedef boost::bimaps::bimap <
    boost::bimaps::unordered_set_of< ExternalAddress >, 
    boost::bimaps::set_of< Address > 
  > ActorRegistryMap;
  
  typedef typename ActorRegistryMap::value_type ActorRecord;
  
  // Then the actual map will be an instance of the actor registry map using
  // the actor record to create new actor records when the information becomes 
  // available.
  
  ActorRegistryMap KnownActors;

  // ---------------------------------------------------------------------------
  // Communication Layers
  // ---------------------------------------------------------------------------
  // 
  // Inbound messages will be forwarded to the Presentation Layer Server and 
  // outbound addresses will be sent to the Network Layer Server. These 
  // addresses are stored for generalised reference, although the objects do 
  // have default names.
  
  Address PresentationServer, NetworkServer;
    
protected: 
  
  // Interface functions for the actor addresses of the Network Layer server
  // and the Presentation Layer server
  
  inline Address GetPresentationLayerAddress ( void ) const
  {
    return PresentationServer;
  }
  
  inline Address GetNetworkLayerAddress ( void ) const
  {
    return NetworkServer;
  }
  
  // In order to check if a node is on this end point, it is necessary to 
  // check with the local EndPoint. A pointer to the endpoint is therefore 
  // kept as for this use also by derived classes.
 
  NetworkEndPoint * Host;
    
  // --------------------------------------------------------------------------
  // Actor address subscriptions
  // --------------------------------------------------------------------------
  //
  // Other local actors may want to know when actors, remote or local, become
  // available and known. For this they can set up a subscription and receive
  // the known actor addresses.
  //
  // The set of active subscribers are kept in a set of subscribers to avoid 
  // double subscriptions and ensure that only one subscription is stored for 
  // each actor.

private:
  
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
  // Address management
  // --------------------------------------------------------------------------
  // 
  // The known actors are kept in the bidirectional address map, and local 
  // actors can query if a given actor address is known.
  
public:
  
  inline bool IsKnownActor( const Address & ActorAddress )
  {
    if ( KnownActors.right.find( ActorAddress ) != KnownActors.right.end() )
      return true;
    else
      return false;
  }
  
  
protected:
  
  // There is a function to store the actor's addresses in the address map.
  // Two issues can occur: 
  // 1) The external address already exits, but the Actor ID is different from
  //    the one already stored => must resolve actor ID
  // 2) The actor ID already exists but the external address is different from
  //    the one already stored => must resolve external address
  // 3) Either the actor ID or the external address is empty. 
  // Since various protocols may be able to recover from these error situations
  // virtual functions for each situation will be implemented.
  //
  // The first function is to resolve the actor ID if the external address 
  // already exists. By default it is replacing the existing actor ID with the 
  // new actor ID
  
  virtual Address ResolveActorID( const ExternalAddress & ExternalActor, 
		   const Address & ExistingActorID, const Address & NewActorID )
  {
    return NewActorID;
  }
  
  // The same behaviour is default for the the external address resolution that
  // only returns the new address to replace the existing external address for 
  // this actor.
  
  virtual ExternalAddress ResolveExternalAddress( const Address & ActorID, 
   const ExternalAddress & ExistingAddress, const ExternalAddress & NewAddress )
  {
    return NewAddress;
  }
  
  // The absence of an address is more complicated since it is in general not 
  // possible to construct an actor ID from an unknown external address type or
  // vice versa. The default versions will simply thrown an invalid argument in 
  // these cases, but for a particular address format it may be possible to do
  // better.
  
  virtual Address ResolveUndefined( const ExternalAddress & ExAddress )
  {
    std::ostringstream ErrorMessage;
    
    ErrorMessage << "Unable to construct actor ID for " << ExAddress;
    
    throw std::invalid_argument( ErrorMessage.str() );
  }
  
  virtual ExternalAddress ResolveUndefined( const Address ActorID )
  {
    std::ostringstream ErrorMessage;
    
    ErrorMessage << "Unable to construct external address for " 
		 << ActorID.AsString();
    
    throw std::invalid_argument( ErrorMessage.str() );
  }
  
  // Then the function to store the addresses can be defined using these 
  // resolvers where appropriate. Once the registration is done subscribers will
  // be notified about this new actor ID if this is a completely 
  // new actor, or if the actor ID is an update for an already existing actor
  // (strange situation, but could happen)
				   
  void StoreActorAddresses ( const Address & TheActorID, 
												     const ExternalAddress & ExAddress )
  {
    // The addresses are first checked for validity and if they can be 
    // constructed based on the given functionality. We have to start with 
    // copies of the function arguments in order to be able to assign to these
    // if necessary. Note that since there is no defined way to check whether 
    // an external address is valid, we compare it against a default 
    // constructed external address assuming that this represents the empty 
    // address state.
    
    Address         NewActorID( TheActorID );
    ExternalAddress NewExternalAddress( ExAddress );
    
    if ( NewActorID == Address::Null() )
      NewActorID = ResolveUndefined( NewExternalAddress );
    
    if ( NewExternalAddress == ExternalAddress() )
      NewExternalAddress = ResolveUndefined( NewActorID ); 
    
    // At this point, the addresses will correspond to defined addresses, and
    // it the pair is inserted into the address map.
    
    bool ActorIDchanged = true;
    auto Result         = KnownActors.insert( 
								          ActorRecord( NewExternalAddress, NewActorID ) );
    
    // If the insert failed, it could be because the full actor record exist 
    // already, i.e. both the external address and the actor ID is know before,
    // OR if one of the components is known and the other has changed and 
    // should be updated. It is not well documented, but the insert function 
    // on a bi-map returns the same pair as a normal map where the first element
    // is an iterator to an existing element, and the second element is a 
    // boolean indicating if the insertion operation was successful or not. 
    
    if ( Result.second == false )
    {
      // In order to address the values of element that made the insertion fail
      // we must first project the element iterator to one of the two views.
      
      auto NewRecord = KnownActors.project_left( Result.first );
    
      // The easy situation is that this pair of elements is already a known 
      // pair for this session layer
      
      if ( (NewRecord->first  == NewExternalAddress) && 
				   (NewRecord->second == NewActorID) )
				ActorIDchanged = false;
      else
      {
				// We can now have one of the following situations:
				// 1) The external address exist before, but is associated with a 
				//    different actor ID. The given actor ID does not exist before
				//    and we should therefore update the actor ID.
				// 2) The given actor ID exists before, but the external address does
				//    not exist before. We should in this case update the external 
				//    address for the given actor ID.
				// 3) Both the external address and the actor ID exists before, but 
				//    we know already that they are not on the same record. Hence, 
				//    this update should replace the two records already in the map,
				//    and a new record should be created.
				// To detect these situations, we look up the address and the ID 
				// independently.
				
				auto ByAddress = KnownActors.left.find(  NewExternalAddress  );
				auto ByID      = KnownActors.right.find( NewActorID );
				
				// The first situation is handled first
				
				if ( (ByAddress != KnownActors.left.end()) &&  
				     (ByID      == KnownActors.right.end()) )
				{
				  NewActorID = ResolveActorID( ByAddress->first,
							       ByAddress->second, NewActorID );
				  
				  // The actor ID is different from the existing ID, the subscribers 
				  // must first be notified that this ID has been removed, and then 
				  // later notified about the new ID (before returning from this 
				  // function
				  
				  if ( NewActorID != ByAddress->second )
				  {
				    for ( Address Subscriber : NewPeerSubscribers )
				      Send( PeerRemoved( ByAddress->second ), Subscriber );
				    
				    KnownActors.left.replace_data( ByAddress, NewActorID );
				  }
				  else
				    ActorIDchanged = false;

				}
				else if ( (ByID      != KnownActors.right.end()) && 
					  (ByAddress == KnownActors.left.end() ) )
				{
				  // The ID exist, but the external address does not, and we can simply
				  // replace the data part of the ByID iterator with the new external
				  // address provided it should change, and note that no new actor ID 
				  // has been seen.
				  
				  NewExternalAddress = ResolveExternalAddress(
				    ByID->first, ByID->second, NewExternalAddress );
				  
				  if ( NewExternalAddress != ByID->second )
				    KnownActors.right.replace_data( ByID, NewExternalAddress );
				  
				  ActorIDchanged = false;
				}
				else
				{
				  // Both the address and the actor ID exist, but obviously not as a 
				  // pair. This may invalidate both records
				  
				  NewActorID = ResolveActorID( ByAddress->first,
							       ByAddress->second, NewActorID );
				  
				  NewExternalAddress = ResolveExternalAddress(
				    ByID->first, ByID->second, NewExternalAddress );
				  
				  // If the ID for the found external address should change, we must
				  // tell the subscribers that the Actor ID associated with the external
				  // address will be removed. and then remove the record
				  
				  if ( NewActorID != ByAddress->second )
				  {
				    for ( Address Subscriber : NewPeerSubscribers )
				      Send( PeerRemoved( ByAddress->second ), Subscriber );
				    
				    ActorIDchanged = true;
				  }
				  else
				    ActorIDchanged = false;

				  // Then we remove the two records
				  
				  KnownActors.left.erase(  ByAddress );
				  KnownActors.right.erase( ByID      );
				  
				  // And a new record is created binding the external address to the
				  // given actor ID based on the outcome of the resolution functions 
				  // above.
				  
				  KnownActors.insert( ActorRecord( NewExternalAddress, NewActorID ) );
				}
      }
    }
    
    // Then we can notify the subscribers if the Actor ID was new
    
    if ( ActorIDchanged && (! NewPeerSubscribers.empty() ) )
      for ( Address Subscriber : NewPeerSubscribers )
				Send( NewPeerAdded( NewActorID ), Subscriber );
  }

public:
  
  // The handler for registration commands from local actors could by default
  // only resolve the external address and store the addresses for this actor.
  // However, different protocols may need to do different actions, so the 
  // handler is virtual and can must replaced by derived protocols because 
  // there it is not possible to know generically how to map the local actor
  // address to an external address.
  
  virtual void RegisterActor( 
	        const SessionLayerMessages::RegisterActorCommand & Command, 
	        const Address LocalActor ) = 0;
  
  // In the same way there is a command handler for removing the external 
  // address of an actor (local or external). There can also be protocol 
  // specific side effects for this action, however the default behaviour is 
  // just to look up the actor in the map and remove it if it has a 
  // registration. The information that the actor has been removed is also 
  // passed back to all subscribers of peer actor information.

  virtual void RemoveActor( 
	        const SessionLayerMessages::RemoveActorCommand & Command,
	        const Address ActorAddress )
  {
    auto AddressRecord = KnownActors.right.find( ActorAddress );

		if (AddressRecord != KnownActors.right.end() )
    {
      KnownActors.right.erase( AddressRecord );
 			
      for ( Address Subscriber : NewPeerSubscribers )
				Send( PeerRemoved( ActorAddress ), Subscriber );
    }
  }
  
protected:
    
  // --------------------------------------------------------------------------
  // Datagram encoding and decoding
  // --------------------------------------------------------------------------
  
  // There is also a function to define the datagram from a serialised message
  // It should return a string that should be a legal payload of the externally
  // transmitted message under the used protocol. The default version represents
  // no protocol, and simply returns the serialised message as it was given.

  virtual std::string EncodeDatagram( const std::string & SerialisedMessage )
  {
    return std::string( SerialisedMessage );
  }
  
  // One will also need to interpret an arriving message and convert it into
  // a data string that can be forwarded to the Presentation Layer server for 
  // de-serialisation. Since some protocols will support commands that are 
  // interpreted and executed by the Session Layer, it may be that the message 
  // should not be forwarded to any other local actor on this EndPoint. This 
  // situation is indicated by returning an empty string. 
  
  virtual std::string DecodeMessage( const ExternalMessage & Message ) = 0;

  // --------------------------------------------------------------------------
  // Address resolution 
  // --------------------------------------------------------------------------
  
  // If a local actor sends a message to a remote actor whose address is 
  // unknown (first message), or if a remote actor sends its first message to 
  // an actor on this endpoint its global actor ID will be unknown. In both 
  // cases the remote endpoint must be queried for the missing piece of the 
  // complete address pair< ActorID, ExternalAddress >.
  // 
  // Resolving the missing information typically takes time. In the meantime
  // more messages can arrive in this transaction. Hence there is a need to 
  // queue the messages while the missing address field is resolved, and then 
  // once the address pair is complete, the queued messages must be released 
  // and forwarded either to the remote actor (outgoing message), or to the 
  // Presentation Layer server (inbound message).
  //
  // To avoid that this resolution process will block communication among other
  // known actors, address resolution actors will be used. Such an actor will
  // be dynamically created upon the first undeliverable message, and commit 
  // suicide once the complete address pair has been received and acknowledged
  // by the Protocol Engine.
  //
  // Since the functionality and interaction with the protocol engine is 
  // mostly the same for both address types, the base class is a template for 
  // the message type to be resolved.
  //
  // Implementation detail: the address resolver need to have the actor class
  // as a virtual base class since the compiler will not see it for derived 
  // classes and claim that derived classes are not actors.
  
private:
  
  template< class MessageType >
  class AddressResolver : virtual public Actor
  {    
    // The actor ID of the remote actor and its remote address. These fields 
    // are protected so that derived classes can use them to complete the 
    // the cached messages before they are forwarded.
    
  protected:
    
    Address         RemoteActorID;
    ExternalAddress RemoteAddress;
    
    // The queue of messages to send once the address has been resolved. No 
    // derived class should need to use this queue directly
    
  private:
    
    std::queue< MessageType > PendingMessages;
    
    // The handler for receiving and queueing messages. As this will be 
    // called from the Theron library, it must be public.
    
  public:
    
    void NewMessage ( const MessageType & TheMessage, 
		      const Address From )
    {
      PendingMessages.push( TheMessage );
    }
    
    // The cached messages are sent once the resolved address is known. 
    // However, to which actor it will be sent depends on the direction this 
    // address resolver is used for. If it is used for incoming messages all
    // messages should be forwarded to the Presentation Layer server, and if 
    // it is used for outgoing messages, all messages should be sent to the 
    // Network Layer server. It is therefore necessary to allow the direction 
    // specific sub-classes to change the message correctly. This function 
    // must be defined
  
  protected:
    
    virtual void SendCachedMessage ( MessageType & TheMessage ) = 0 ;
    
    // This will be used by the function that will empty the cached, i.e. 
    // send all the messages stored in the message queue. It will first be 
    // invoked by the handler receiving the missing address information, as 
    // defined for the concerned sub-classes. 
      
    void EmptyCache( void )
    {
      while ( !PendingMessages.empty() )
      {
				SendCachedMessage( PendingMessages.front() );
				PendingMessages.pop();
      }
    }
    
    // The Session Layer server will respond with a boolean (true) when the 
    // resolved address pair has been stored. This will make all future messages 
    // to be handled directly by the Session Layer server. However, there could 
    // be messages of this dialogue arriving to the queue after we emptied the 
    // cache, and before the receipt of this completion flag. This implies that 
    // we have to empty the cache again when we receive the flag, just to be on 
    // the safe side. It should be noted that the Session Layer server should 
    // not ask the resolver to queue any more messages after sending the 
    // completion flag as the messages will be directly handled by the Session
    // Layer server from then onwards, i.e. we have the invariant that
    // when this handler is invoked, the resolver actor can be destroyed as it
    // has completed its operation.
    
  public:
    
    void CompleteOperation ( const bool & flag, Address From )
    {
       EmptyCache();
       delete this;
    }
    
    // The constructor takes the framework for the actors. It will register the 
    // handler for new messages that may be generated while the resolution 
    // takes place, and the handler for the completion of the resolution 
    // process. Note that the address fields will have to be initialised by 
    // the classes knowing which address field to resolve and which is the 
    // known field.
    
    AddressResolver( Framework & HostingFramework, 
		     const Address & ActorID = Theron::Address::Null(),
		     const ExternalAddress & GlobalAddress = ExternalAddress() )
    : Actor( HostingFramework ), 
      RemoteActorID( ActorID ), RemoteAddress( GlobalAddress )
    { 
      RegisterHandler(this, 
      &SessionLayer<ExternalMessage>::AddressResolver<MessageType>::NewMessage 
      );
      
      RegisterHandler(this, 
      &SessionLayer<ExternalMessage>::AddressResolver<MessageType>::CompleteOperation
      );
    }
  };
  
protected:
  
  // The addresses of resolution actors will be kept in two maps: one map for 
  // resolution actors for outgoing messages where the actor ID of the remote
  // actor is mapped to the actor ID of the resolution actor so we know which 
  // resolution actor that holds the queue of pending messages for this remote
  // actor.
  
  std::map< Address, Address > OutboundResolutionActors;
  
  // There is a similar map for inbound messages where we lookup the given 
  // external address, and find out the Theron address of the resolution actor 
  // that is used to cache such messages.
  
  std::map< ExternalAddress, Address > InboundResolutionActors;

public:
    
  // Addresses and IDs of external actors must be resolved by remote endpoints, 
  // and the result is communicated back as a pair consisting of the actor ID 
  // and its external address. In this case the From address will be the 
  // local actor responsible for resolving the remote address. This 
  // handler will register the remote actor as a known actor, and 
  // remove the resolution actor from the resolution actors so 
  // that future communication for the remote actor will go directly 
  // to the remote actor.

  void StoreRemoteAddress ( 
      const std::pair< Address, ExternalAddress > & ResolvedAddress,
      const Address From )
  {

    // The address and ID of the actor is registered as a known actor.

    StoreActorAddresses( ResolvedAddress.first, ResolvedAddress.second );

    // Then the command is acknowledged so that the resolution actor can 
    // send the messages it has stored and shut down. 
    
    Send( true, From );
    
    // Finally, we forget about the resolution actor involved with this 
    // resolution. Since this is a generic method where we do not know if 
    // the direction of the communication resolved, we will need to check
    // both maps. The outbound map is checked first since the key is an ID
    // and comparing IDs may be faster than comparing external addresses that
    // could be strings.
    
    auto OutResolver = OutboundResolutionActors.find( ResolvedAddress.first );
    
    if ( ( OutResolver != OutboundResolutionActors.end() ) && 
         ( OutResolver->second != From )    ) 
      OutboundResolutionActors.erase( OutResolver );
    else
    {
      auto InResolver = InboundResolutionActors.find( ResolvedAddress.second );
      
      if ( ( InResolver != InboundResolutionActors.end() ) &&
				   ( InResolver->second != From ) )
				InboundResolutionActors.erase( InResolver );
    }
  };

  // --------------------------------------------------------------------------
  // Outbound: Remote address resolution 
  // --------------------------------------------------------------------------
  
  // The most complex behaviour occurs when the address of a remote actor is 
  // not known. Then this must be resolved, and this process typically entails 
  // querying either a central address repository, or each known EndPoint to 
  // have the correct remote address returned over the network. This typically
  // takes time, and the protocol engine should not block waiting for this 
  // address resolution to complete. To avoid waiting, it creates a temporary 
  // actor responsible for this remote address resolution, and leave to this 
  // actor to send the message(s) once the remote address is known. Since more 
  // messages could be sent to the same unknown remote actor before its address
  // has been resolved, its handling actor will queue the messages and send 
  // them all. Once the queue of messages is empty, the actor will commit 
  // suicide and disappear from the system.  

protected:
  
  class OutboundAddressResolver 
  : virtual public Actor, public AddressResolver< ExternalMessage >
  {
  public:
    
    typedef AddressResolver< ExternalMessage > ResolverType;
    
  private:
    
    // The resolver needs to know the address of the Session Layer server for 
    // registering the new address found, and to the Network Layer server to 
    // send the message(s).
    
    Address SessionServer, NetworkServer;
    
  protected:
    
    // A cached message is completed with the now known address and sent
    
    void SendCachedMessage( ExternalMessage & TheMessage )
    {
			TheMessage.SetRecipient( ResolverType::RemoteAddress );
			Send( TheMessage, NetworkServer );
    }
    
    // There is a handler for the external address to be used for this remote
    // actor ID. When this is invoked, it will store the address locally, 
    // and then notify the Session Layer server that the address has been 
    // resolved. This should also make this actor to be de-registered in the 
    // Session Layer server.
    
  public:
    
    void ResolvedAddress ( const ExternalAddress & ResolvedAddress, 
			   const Address From )
    {
      ResolverType::RemoteAddress = ResolvedAddress;
      ResolverType::EmptyCache();
      
      Send( std::make_pair( ResolverType::RemoteActorID, ResolvedAddress ), 
	    SessionServer );
    }
        
    // The constructor takes the framework for the actors, and the actor ID 
    // of the remote to resolve the address for. It will register the handler 
    // for new messages that may be generated while the resolution takes place,
    // and the handler for the outcome of the address resolution.
    
    OutboundAddressResolver( Framework & HostingFramework, 
			     const Address & TheRemoteActorID,
			     const Address & NetworkServerActor = 
					     Address( "NetworkLayer" ),
			     const Address & SessionServerActor = 
					     Address( "SessionLayer" )	  )
    : Actor( HostingFramework ),
      ResolverType( HostingFramework, TheRemoteActorID ), 
      SessionServer( SessionServerActor ), NetworkServer( NetworkServerActor ) 
    {      
      RegisterHandler( this, & OutboundAddressResolver::ResolvedAddress );
    }
    
    // The destructor simply removes the message handler and terminates
    
    virtual ~OutboundAddressResolver ( void )
    {
      DeregisterHandler( this, &OutboundAddressResolver::ResolvedAddress );
    }
    
  };
  
  // These actors are created by a virtual function that must be overloaded 
  // because the the necessary steps to initiate the address resolution must 
  // be taken, and it could also be that the user would want to extend the 
  // resolution actor with added functionality, so that there is a derived 
  // class that will be created. The only thing needed back from the creator
  // function is the ID of the actor. 
  
  virtual Address CreateOutboundResolver( 
    const Address & UnknownActorID, const Address & NetworkServerActor,
    const Address & SessionServerActor
  ) = 0;
    
  // --------------------------------------------------------------------------
  // Outbound: message handling
  // --------------------------------------------------------------------------
  
  // When messages are to be sent to remote actors, they will first be delivered
  // to the Presentation Layer server to be serialised, and then they will be 
  // sent to this Session Layer to be encoded and sent to the Network Layer. 
  // These operations are carried out by the following handler.
  
public:
  
  void OutboundMessage( const PresentationLayer::SerialMessage & TheMessage,
			const Address From                     )
  {
    ExternalMessage LinkMessage;
    
    // The datagram can be directly encoded.
    
    LinkMessage.SetPayload( EncodeDatagram( TheMessage.GetPayload() ) );
      
    // First we check if the global address of the sending actor is known, 
    // and if it is not, then the remote address is created for the sender 
    // actor.
    
    auto ExAddress = KnownActors.right.find( TheMessage.GetSender() );
    
    if ( ExAddress == KnownActors.right.end() )
    {
      RegisterActor( SessionLayerMessages::RegisterActorCommand(), 
		     TheMessage.GetSender() );
      ExAddress = KnownActors.right.find( TheMessage.GetSender() );
    }
        
    // Then the external message can be completed for the sender. Note that 
    // since ExAddress was a lookup by the Actor ID, its "first" is the Actor 
    // ID and its second part is the external address.
    
    LinkMessage.SetSender( ExAddress->second );
    
    // We have to check if the remote actor is already know to us by its 
    // remote actor ID
    
    auto RemoteRecipient = KnownActors.right.find( TheMessage.GetReceiver() );
    
    // If the address of the remote recipient is known already we can simply 
    // complete the external message and pass it on to the link server. The 
    // same argument for using the 'second' field of the lookup pointer holds 
    // here.
    
    if ( RemoteRecipient != KnownActors.right.end() )
    {
      LinkMessage.SetRecipient( RemoteRecipient->second );
      Send( LinkMessage, NetworkServer );
    }
    else
    {
      // If the remote recipient address is not known, it must be resolved, 
      // and this typically involves finding out which node it is that hosts
      // the concerned actor. In general this implies querying all endpoints, 
      // and collect the returned response from the endpoint hosting the
      // actor. To further complicate the issue, it can be several messages 
      // sent to the same remote actor before the remote address resolution 
      // is completed. 
      
      // The first thing to check is if there is a resolution actor already 
      // created for this remote actor, and if there is, the message is sent
      // to that actor. Otherwise the resolution actor must be created and 
      // saved before we can send the message to it.
      
      auto ExistingResolver = OutboundResolutionActors.find( 
						     TheMessage.GetReceiver() );
      
      if ( ExistingResolver == OutboundResolutionActors.end() )
      {
				auto CreationResult = OutboundResolutionActors.emplace( 
					TheMessage.GetReceiver(), 
					CreateOutboundResolver( TheMessage.GetReceiver(), NetworkServer, 
																	GetAddress() ) );
		
				ExistingResolver = CreationResult.first;
      }
      
      // Then we can forward the message to the resolver actor for this 
      // remote actor
      
      Send( LinkMessage, ExistingResolver->second );
    }
  } 

  // --------------------------------------------------------------------------
  // Inbound: Remote actor ID resolution
  // --------------------------------------------------------------------------

  // As described in the introduction, we also need to resolve the ID of a 
  // remote actor sending a message to this endpoint when the first packet 
  // arrives from that remote ID. The mechanism used is similar to the one
  // used for outbound messages: The external sender address will be represented
  // by a temporary actor that will store all arriving messages until the ID 
  // of the remote actor is obtained, in which case it will transfer all the 
  // messages on hold and register the ID with the protocol engine before 
  // committing suicide.
  
protected:
  
  class InboundIDResolver 
  : virtual public Actor, 
    public AddressResolver< PresentationLayer::SerialMessage >
  {
  public:
    
    typedef AddressResolver< PresentationLayer::SerialMessage > ResolverType;
    
  private:
    
    // The resolver needs to know the address of the Presentation Layer server 
    // for forwarding messages and to the Session Layer server for registering 
    // found remote addresses.
    
    Address PresentationServer, SessionServer;
    
  protected:
    
    // The cached messages are sent to the Presentation Layer server once 
    // the resolved ID is known.
    
    void SendCachedMessage( PresentationLayer::SerialMessage & TheMessage )
    {
			PresentationLayer::SerialMessage 
			ForwardMessage( ResolverType::RemoteActorID, TheMessage.GetReceiver(),
										  TheMessage.GetPayload()		);
			
			Actor::Send( ForwardMessage, PresentationServer );
    }

    // There is a handler to update the actor ID of the remote actor and 
    // inform the Session Layer server about this discovery. Any queued messages 
    // are also sent to the Presentation Layer sever for further relay to the 
    // actual actors receiving these messages. 
    
  public:
    
    void ResolvedID ( const Address & RemoteID, const Address From )
    {
      ResolverType::RemoteActorID = RemoteID;
      ResolverType::EmptyCache();
      
      Send( std::make_pair( RemoteID, ResolverType::RemoteAddress ), 
	    SessionServer );
    }

    // The constructor takes the known remote address as parameter, and 
    // register the resolved ID handler.
    
    InboundIDResolver( Framework & HostingFramework, 
								       const ExternalAddress & TheRemoteActorAddress,
								       const Address & PresentationLayerActor = 
											       Address("PresentationLayer"),
								       const Address & SessionLayerActor = 
											       Address("SessionLayer")    )
    : Actor( HostingFramework ),
      ResolverType( HostingFramework, 
		    Theron::Address::Null(),  TheRemoteActorAddress ), 
      PresentationServer( PresentationLayerActor ),
      SessionServer( SessionLayerActor )
    {
      RegisterHandler(this, 
					& SessionLayer<ExternalMessage>::InboundIDResolver::ResolvedID
      );
    }
  };
  
  // The resolver is also in this case allocated by a creator that returns 
  // the ID to use when forwarding messages to be queued by this actor.
  
  virtual Address CreateInboundResolver( 
	const ExternalAddress & UnknownActorAddress,
	const Address & PresentationLayerActor = Address("PresentationLayer"),
	const Address & SessionLayerActor = Address("SessionLayer") ) = 0;
  
  // --------------------------------------------------------------------------
  // Inbound: message handling
  // --------------------------------------------------------------------------
  // When a message arrives from the Network Layer server, its datagram can be 
  // a message for one of the local actors, or a command which is a part of 
  // the protocol (for instance a request for the external address of a 
  // local actor). The datagram is therefore decoded first, and if it returns 
  // a proper payload, the message will be forwarded to the Presentation Layer 
  // server for de-serialising and final forwarding to the local actor. 
  
public:
  
  void InboundMessage( const ExternalMessage & TheMessage, const Address From )
  {
    std::string Payload = DecodeMessage( TheMessage );
    Address ReceiverActor;
    
    if ( !Payload.empty() )
    {
      // This is a message and not a protocol command, so we can proceed to 
      // find the local actor to receive this message. This address should 
      // exist because when the remote Session Layer server receives this 
      // message for an actor on this endpoint, it will resolve the address for 
      // this actor which involves this endpoint's Session Layer. We should 
      // therefore know that the message is coming for a given actor ID, and 
      // this Session Layer shall register the external address for its local 
      // actor in the resolution process.
      
      auto LocalActor = KnownActors.left.find( TheMessage.GetRecipient() );
      ReceiverActor   = LocalActor->second;
      
      // The senders ID could be more problematic if this is the first message
      // from the remote actor received at this endpoint.
      
      auto RemoteID = KnownActors.left.find( TheMessage.GetSender() );
      
      // If we know the remote actor's ID already we can happily forward the 
      // message for de-serialisation.
      
      if ( RemoteID != KnownActors.left.end() )
				Send( PresentationLayer::SerialMessage( 
				      RemoteID->second, ReceiverActor, Payload ), PresentationServer );
      else
      {
				// The remote ID is either under resolution or has to be resolved. 
				// In either case the message can only be queued with the resolver, 
				// which will take care of forwarding the message to the Presentation 
				// Layer server once the remote ID is known.
				
				auto ExistingResolver = 
						InboundResolutionActors.find( TheMessage.GetSender() );
				
				if ( ExistingResolver == InboundResolutionActors.end() )
				{
				  auto CreationResult = InboundResolutionActors.emplace(
				    TheMessage.GetSender(), 
				    CreateInboundResolver( TheMessage.GetSender(),
							   PresentationServer, GetAddress() ) 
				  );
				  
				  ExistingResolver = CreationResult.first;
				}
				
				// Now a resolver actor exist so we can enqueue the message with this 
				// resolver.
				
				Send( TheMessage, ExistingResolver->second );
      }
    }
  }

  // --------------------------------------------------------------------------
  // Constructor and destructor
  // --------------------------------------------------------------------------

  // Since the Session Layer server will send and receive messages from both the 
  // Network Layer server and the Presentation Layer server one could imagine 
  // that the addresses of these two actors were passed as arguments to the 
  // constructor. However, both the Network Layer server and the Presentation 
  // Layer server also needs the Session Layer's address, which will make it 
  // impossible to find an order of creation of these three classes. The 
  // binding must therefore be done after the class creation using the 
  // special interface functions.
  
  inline void SetNetworkLayerAddress( const Address & NetworkServerActor )
  {
    NetworkServer = NetworkServerActor;
  }
  
  inline void SetPresentationLayerAddress( 
				      const Address & PresentationServerActor )
  {
    PresentationServer =  PresentationServerActor;
  }
  
  // The constructor then takes only the name of the Session Layer as 
  // argument after the mandatory pointer to the network endpoint. However, 
  // note that there is a default initialisation of the addresses of the 
  // Network Layer server and the Presentation Layer server. This is possible 
  // since a Theron Address  object initialised with a string will not check if 
  // this corresponds to a legal address before a message is being sent to this 
  // address. Hence, if the default names are used, then the explicit binding 
  // of their addresses is not necessary.
  
  SessionLayer( NetworkEndPoint * HostPointer,
								const std::string & ServerName = "SessionLayer"  )
  : Actor( HostPointer->GetFramework(), ServerName.data() ), 
    StandardFallbackHandler( HostPointer->GetFramework(), ServerName ),
    SessionLayerMessages(), 
    KnownActors(), 
    PresentationServer(), NetworkServer(),
    Host( HostPointer ),
    NewPeerSubscribers(),
    OutboundResolutionActors(), InboundResolutionActors()
  { 
    RegisterHandler( this, &SessionLayer<ExternalMessage>::RegisterActor            );
    RegisterHandler( this, &SessionLayer<ExternalMessage>::RemoveActor        	    );
    RegisterHandler( this, &SessionLayer<ExternalMessage>::StoreRemoteAddress 	    );
    RegisterHandler( this, &SessionLayer<ExternalMessage>::OutboundMessage    	    );
    RegisterHandler( this, &SessionLayer<ExternalMessage>::InboundMessage           );
    RegisterHandler( this, &SessionLayer<ExternalMessage>::SubscribeToPeerDiscovery );
    RegisterHandler( this, &SessionLayer<ExternalMessage>::UnsubscribePeerDiscovery );
  }
  
  // The destructor will de-register the message handlers and leave the other
  // containers to clean up their own storage. Note that this is not strictly 
  // necessary as Theron will clean up any message handlers still registered 
  // when the actor closes.
  
  virtual ~SessionLayer()
  {
    DeregisterHandler( this, &SessionLayer<ExternalMessage>::RegisterActor            );
    DeregisterHandler( this, &SessionLayer<ExternalMessage>::RemoveActor              );
    DeregisterHandler( this, &SessionLayer<ExternalMessage>::StoreRemoteAddress       );
    DeregisterHandler( this, &SessionLayer<ExternalMessage>::OutboundMessage          );
    DeregisterHandler( this, &SessionLayer<ExternalMessage>::InboundMessage   	      );    
    DeregisterHandler( this, &SessionLayer<ExternalMessage>::SubscribeToPeerDiscovery );
    DeregisterHandler( this, &SessionLayer<ExternalMessage>::UnsubscribePeerDiscovery );
  }
};
  
} 	// End name space Theron
#endif 	// THERON_SESSION_LAYER
