/*=============================================================================
  Network Layer

  The purpose of the network layer is to manage the interface to the outside. It
  will most likely build on some other library managing low level stuff like 
  sockets and IP protocols. An implementation will probably also run a thread 
  which will be reacting to incoming packets. 
  
  The provided code is therefore mainly a stub defining some hooks for derived
  classes to implement the necessary protocol level functionality without 
  having to understand Theron, or the way the hierarchy of communication actors
  interact in order to serve the actors.
  
  It could even be that this implements some low level protocol. One typical 
  example is that the protocol engine assumes that each externally communicating
  actor has a unique external address. However, if the link is implemented as
  one single socket on one single IP, all the actors share the same IP. In this
  case the unique protocol address could be to send a message to
  "ActorID@192.168.10.12:445" which must be understood as by the link server as
  IP=192.168.10.12, Port=445 and then the ActorID must be added to the datagram.
  In other words, it can well be also a link level protocol that must be 
  implemented by this actor before sending the message, or before delivering a
  received message to the protocol engine.
  
  Author: Geir Horn, University of Oslo, 2015
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef THERON_NETWORK_LAYER
#define THERON_NETWORK_LAYER

#include <string>
#include <type_traits>
#include <Theron/Theron.h>

#include "NetworkEndPoint.hpp"
#include "LinkMessage.hpp"

#include <ostream>  // TEST for debugging information
#include <iostream> // TEST for debugging information

namespace Theron
{

template< class ExternalMessage >
class NetworkLayer : public Actor
{
public: 
  
  typedef ExternalMessage MessageType;
  
  // For the link server it is not essential that the external message confirms
  // to the LinkMessage interface, but it is a requirement for the protocol 
  // engine, hence the condition is enforced also here.
  
    static_assert( std::is_base_of< 
    LinkMessage< typename ExternalMessage::AddressType >, 
    ExternalMessage >::value,
    "NetworkLayer: External message must be derived from LinkMessage" );
  
protected:
  
  // The address of the corresponding session layer is stored to allow 
  // the flexibility in naming it.
  
  Address SessionServer;
  
public:

  // The link server will forward incoming messages to the session layer 
  // and it would be tempting to provide the address of the session layer 
  // to the constructor. However, also the session layer will need the address
  // of the network layer, making it impossible to find an order of construction 
  // for the two objects. An interface function is therefore provided to 
  // allow explicit binding of the objects after they have been created.
  // 
  // The binding between the Network Layer and the Session Layer is done by 
  // the Network End Point after creating each actor type by directly setting
  // the Session Server variable. However since the servers for both layers 
  // will only be known by derived classes, an interface function is necessary
  // and will be used from a class derived from the Network End Point.  
  
  inline void SetSessionLayerAddress( const Address & SessionLayerActor )
  {
    SessionServer = SessionLayerActor;
  } 

  // Outgoing messages will be sent using a function receiving the addresses
  // of the two parties of the communication and the message datagram
  // containing the protocol embedded payload.
  
  virtual void SendMessage( const ExternalMessage & TheMessage ) = 0;
			    
  // In the same way, we support the reception of messages and forward 
  // this directly to the session layer.
			    
  void ProcessMessage( const ExternalMessage & TheMessage )
  {
    std::cout << "Message received from " << TheMessage.GetSender()
		 << " to " << TheMessage.GetRecipient() << " with subject = ["
		 << TheMessage.GetSubject() << "] and body = ["
		 << TheMessage.GetPayload() << "]" << std::endl;
    
    Send( TheMessage, SessionServer );
  }
  
  // There is a handler to receive the messages from the session server
  // This only calls the Send Message function as a way to separate the 
  // Theron specific functionality from the link level protocol.
  
  void OutboundMessage( const ExternalMessage & TheMessage, const Address From )
  {
    SendMessage( TheMessage );
  }
    
  // The constructor initialises the actor making sure that it has the right
  // name, and registers the handler for messages received from the protocol
  // engine. The address for the session layer server is initialised with the 
  // default session layer address. This is possible since Theron Address 
  // will not check if the object exists if initialised with a string. 
  
  NetworkLayer( NetworkEndPoint * Host,
	        std::string ServerName = "NetworkLayer"  ) 
  : Actor( Host->GetFramework(), ServerName.data() ),
    SessionServer( Address( "SessionLayer" ) )
  {
    RegisterHandler( this, &NetworkLayer< ExternalMessage >::OutboundMessage );
  }
  
  // Finally there is a virtual destructor 
  
  virtual ~NetworkLayer( void )
  { };
};
  
  
}      // End namespace Theron
#endif // THERON_NETWORK_LAYER