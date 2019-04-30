/*=============================================================================
  Link Message
  
  The link message is the format of the message sent on the link, i.e. the 
  encapsulation of the payload of the package. The base class defined in 
  this file defines the interface functions that must be implemented on this
  message in order to ensure that it minimally contains
  
  1) The remote receiver address
  2) The sender address
  3) A payload string
  
  It is defined as a template for the format of the external address. This 
  can be a string for IP adresses, but it can be anything that the link supports
  like an integer.
  
  The link message is formatted by the Session Layer and sent by the 
  Network Layer. Incoming messages are received by the Network Layer and 
  forwarded as a link message object to the Session Layer. 
  
  Author: Geir Horn, University of Oslo, 2015
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef THERON_LINK_MESSAGE
#define THERON_LINK_MESSAGE

#include <string>                // Standard strings

#include "Actor.hpp"             // The Theron++ actor framework
#include "SerialMessage.hpp"     // Messages supporting serialisation

namespace Theron
{
 
template < class ExternalAddress >
class LinkMessage
{
protected:
	
	// The message is only valid if it has a sender, a receiver and a payload 
	// set. Hence these are parameters to the constructor, and read only fields 
	// for derived messages. 
	
	const ExternalAddress        SenderAddress, ReceiverAddress;
	const SerialMessage::Payload Payload;
	
public:
  
  using AddressType = ExternalAddress;

  // One should be able to obtain the payload string of the message.
  
  inline SerialMessage::Payload GetPayload( void ) const
  { return Payload; }
  
  // There are similar functions to obtain or set the sender and the receiver of 
  // this message.
  
  inline ExternalAddress GetSender   ( void ) const
  { return SenderAddress; }
  
  inline ExternalAddress GetRecipient( void ) const
  { return ReceiverAddress; }
  
  // The link message provides a function to convert an external address to 
  // an actor address. The issue is that when the session layer receives a 
  // message from a sender that has not been registered, it must forward this
  // to the local actor with the actor address of the remote sender as the 
  // sender's address. This conversion is actually dependent on the external 
  // address format. It could have been done by the external address, but that 
  // would force that address to be a class, and so an implementation is 
  // enforced here since the link message will always be a class.

  virtual 
  Address ActorAddress( const ExternalAddress & ExternalActor ) const = 0;

  // The standard constructor takes the Sender address, the Receiver address 
	// and the payload and initialises the various fields accordingly.
	
	LinkMessage( const ExternalAddress & From, const ExternalAddress & To, 
							 const SerialMessage::Payload ThePayload )
	: SenderAddress( From ), ReceiverAddress( To ), Payload( ThePayload )
	{ }
	
	// A message can also be copied from another message, in which case it 
	// simply delegates the initialisation to the standard constructor.
	
	LinkMessage( const LinkMessage & Other )
	: LinkMessage( Other.SenderAddress, Other.ReceiverAddress, Other.Payload )
	{ }
	
	// The default constructor is explicitly deleted as it should not be used.
  
  LinkMessage( void ) = delete;
	
	// The destructor is virtual to support correct deallocation of derived 
	// classes, but it does not do anything in particular.
	
	virtual ~LinkMessage( void )
	{ }
};
    
};			// namespace Theron
#endif 	// THERON_LINK_MESSAGE
