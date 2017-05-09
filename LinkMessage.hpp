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

#include <string>
#include <Theron/Theron.h>

namespace Theron
{
 
template < class ExternalAddress >
class LinkMessage
{
public:
  
  typedef ExternalAddress AddressType;

  // Default dummy constructor   
  
  LinkMessage( void )
  { };
  
  // One should be able to obtain or set the payload string on the message.
  
  virtual std::string GetPayload( void ) const = 0;
  virtual void SetPayload( const std::string & Payload ) = 0;
  
  // There are similar functions to obtain or set the sender and the receiver of 
  // this message.
  
  virtual ExternalAddress GetSender   ( void ) const = 0;
  virtual ExternalAddress GetRecipient( void ) const = 0;
  
  virtual void SetSender   ( const ExternalAddress & From ) = 0;
  virtual void SetRecipient( const ExternalAddress & To   ) = 0;
  
  // Finally there is an operator to take care of the initialisation in one 
  // go. It is by default defined by the other interface functions. It takes
  // the parameters in the order of the Theron Framework's Send function 
  // starting with the payload and then give the sender and receiver.
  
  virtual void operator() ( const std::string     & ThePayload, 
			    const ExternalAddress & From,
			    const ExternalAddress & To )
  {
    SetPayload  ( ThePayload );
    SetSender   ( From );
    SetRecipient( To );
  };
};
    
};			// namespace Theron
#endif 	// THERON_LINK_MESSAGE
