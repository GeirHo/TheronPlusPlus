/*=============================================================================
Active Message Queue (AMQ): Messages

The fundamental protocol is that each Actor that can be addressed globally 
needs an inbox as a topic on the AMQ broker. This inbox is named with the 
global address of the Actor receiving messages. This necessitates a global 
addressing scheme for Actors, and the global address is defined in this file 
together with the message used between the Session Layer and the Network Layer.

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
=============================================================================*/

#ifndef THERON_AMQ_MESSAGES
#define THERON_AMQ_MESSAGES

// Standard headers

#include <string>             // For standard strings
#include <compare>            // For global address comparison
#include <memory>             // For smart pointers

// Qpid Proton headers

#include <proton/message.hpp> // AMQ message format

// Headers for the Theron++ actor system

#include "Actor.hpp"
#include "Communication/LinkMessage.hpp"

namespace Theron::AMQ
{

/*==============================================================================

  Global Actor addresses

==============================================================================*/
//
// The global addresses of actors are supposed to be unique, and although all
// actors on the same endpoint can be reached through the same in-box of the
// end point hosting the actor, it prevents us from re-using the standard
// functionality of the session layer mapping actor addresses to external
// addresses. Thus the external address i defined analogous to the Jabber ID
// as "actor@endpoint".

class GlobalAddress
: public Theron::GlobalAddress
{
public:

  // There are two cases to consider when reporting this as a string: the 
  // address could be for a remote actor on a given endpoint, but it could
  // also be just an AMQ topic. In the latter case there is no endpoint to 
  // identify the location of the topic, and the @endpint should not be a 
  // part of the string representation of the topic name.
  
  virtual std::string AsString( void ) const override
  {  
    if ( EndpointID.empty() )
      return ActorID;
    else
      return ActorID + '@' + EndpointID; 
  }
  
  // Constructors. 

  inline
  GlobalAddress( const Address & TheActor, const Address & TheEndpoint )
  : Theron::GlobalAddress( TheActor, TheEndpoint )
  {}

  inline
  GlobalAddress( const Address & TheActor, const std::string & TheEndpoint )
  : Theron::GlobalAddress( TheActor, TheEndpoint )
  {}

  inline
  GlobalAddress( const std::string & TheActor, const std::string & TheEndpoint )
  : Theron::GlobalAddress( TheActor, TheEndpoint )
  {}

  inline GlobalAddress( const GlobalAddress & Other )
  : Theron::GlobalAddress( Other )
  {}

  inline GlobalAddress( const std::string & StringAddress )
  : Theron::GlobalAddress( StringAddress.substr( 0, StringAddress.find('@') ),
                           StringAddress.substr( StringAddress.find('@')+1 ) )
  {}

  GlobalAddress( void ) = delete;
  virtual ~GlobalAddress( void )
  {};
};

/*==============================================================================

  Message class

==============================================================================*/
//
// The message is exchanged between the Network Layer and the Session Layer, 
// and contains the global addresses of the sender and the receiver. In addition
// there is a pointer to an AMQ message. A pointer is used because it will be 
// stored in several places, and also if there are many local Actors subscribing
// to the same remote sender, then it will be easier for the Session Layer to 
// distribute the pointer avoiding multiple copies of the message.
// 
// The network layer will add the sender address as the reply-to field of the 
// message before sending, and when receiving a message it will convert the 
// reply-to field to a proper address and add it to the message. 
//
// The message definition can in this case nicely be implemented by the standard
// Link Message. 

class Message 
: public LinkMessage< GlobalAddress, std::shared_ptr< proton::message > >
{
public:

  // There is a small utility function to generate an "handle" to the 
  // message, which is in practice a shared pointer and here there is
  // no difference.

  static inline PayloadType NewMessageHandle( void )
  { return std::make_shared< proton::message >(); }

  // The constructor for the message requires also the sender and the receiver
  // addresses in the global address format.
    
  Message( const GlobalAddress & From, const GlobalAddress & To, 
           const std::shared_ptr< proton::message > TheMessage )
  : LinkMessage< GlobalAddress, std::shared_ptr< proton::message > >
    ( From, To, TheMessage )
  {}
  
  Message() = delete;
  virtual ~Message() = default;
};

} // End name space Theron++ AMQ
#endif // THERON_AMQ_MESSAGES

