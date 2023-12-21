/*==============================================================================
Polymorphic message

A message that is transmitted over the network must be serialised in some way.
However, there are implicit or explicit serialisation. Some network protocols
support the intrinsic conversion between data types at the sender and receiving 
end of the connection irrespective of the programming languages and hardware 
platforms in use. This means an implicit serialisation. 

It will be wasteful to serialise messages on the same computer. When an Actor 
sends a message to another Actor it should not know if the receiver is on the 
same endpoint or at a remote endpoint. This should be automatically handled. 
The polymorphic message is the base class for messages that may or may not end 
up being transmitted over the network. 

The polymorphic message must provide a function to resolve the address of the 
Presentation Layer Actor for the specific type of message. If the receiver of 
a message is not found on the local endpoint, the message will be sent to the 
Presentation Layer, and the presentation layer will convert the message to 
the Payload for the given network protocol. In the case of explicit 
serialisation, this means to encode the message as a string payload. 

When a message arrives from a remote Actor, the actual binary message format 
will depend on the type of message supported by the receiving Actor. An Actor
that is able to receive a message type from a remote Actor, must derive the 
message type from the Polymorphic message, and provide a function capable of 
initialising the binary message from the values in the Payload. This means 
that each derived class must be default constructable, and then implement the 
initialiser function allowing the class to be initialised by the given payload.

Note that one payload can be used to initialise various message types that 
can only be deduced at runtime by the actual network protocol specific tags. 
A prime example of this is a string representing a serialised message. Any 
message type can in principle be serialised, but when a string is received its
content should match only one binary message for the receiving Actor. The 
initialisation function therefore returns a boolean to indicate if the 
initialisation was successful.

A correctly implemented message type for a given protocol should encode the 
message type in the payload to allow the initialiser function to check quickly 
if there is a point in decoding the full payload. To give an example: Consider 
a protocol sending serialised JSON messages, then the payload (the string) can
always be decoded to a JSON message, but different JSON message will have 
different attribute-value pairs. For a particular message, one must therefore 
first decode the string to a generic JSON object, then check if the attributes 
matches the one expected for the given message type and then assign the 
attribute values from the values of the generic JSON object if they match. The 
point is that one should avoid constructing the generic JSON object if the 
given payload string corresponds to a different message, and if it corresponds
to the message being initialised, it should be directly decoded to the message's
JSON class. For more details, see the example for the AMQ interface.

Actors that are supporting external communication should be derived from the 
Networking Actor template class with the network protocol payload. This 
Networking Actor class will define the message hander for the polymorphic 
message for the given protocol payload.

Author and Copyright: Geir Horn, University of Oslo
License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
==============================================================================*/

#ifndef THERON_POLYMORPHIC_MESSAGE
#define THERON_POLYMORPHIC_MESSAGE

#include <type_traits>               // Compile time message checking
#include <string>                    // Standard strings
#include <source_location>	         // For error reporting
#include <sstream> 				           // Formatted errors 

#include <boost/core/demangle.hpp>

#include "Actor.hpp"

namespace Theron 
{
  
/*==============================================================================

 Polymorphic Protocol Handler

==============================================================================*/
//
// In order to send the message to the right presentation layer class in case 
// there are multiple network protocols used at the same time, the message 
// class must provide the address of its handling presentation layer. Since 
// the payload of the different network protocols are also different, then 
// this virtual function must be defined in a base class for all payload 
// dependent messages.
//
// It also defines a error information function that should be overloaded by 
// the polymorphic message to indicate the payload type in the error reporting

class PolymorphicProtocolHandler
{
protected:
  
  virtual Address     PresentationLayerAddress( void ) const = 0;
  virtual std::string GetProtocolTypeName( void ) const = 0;
  
	// These functions should be callable from the presentation layer 
  
	template< typename > friend class PresentationLayer;

public:
  
  PolymorphicProtocolHandler()          = default;
  virtual ~PolymorphicProtocolHandler() = default;
};

/*==============================================================================

 Polymorphic Message

==============================================================================*/
//
// The polymorphic message defines the payload type, and indicates that it is 
// a polymorphic message. Then it defines the functions to convert the message 
// to the payload type, and to initialise the message from the payload type. 
// Not that the message trying to initialise a message is not allowed to 
// throw any exceptions. A failure should be indicated by the return value.

template< class ProtocolPayload >
class PolymorphicMessage
: public PolymorphicProtocolHandler
{
public:
  
  using IsPolymorphic = std::true_type;
  using PayloadType   = ProtocolPayload;
  
protected:
  
  virtual ProtocolPayload GetPayload( void ) const = 0;
  virtual bool Initialize( const ProtocolPayload & ThePayload ) noexcept = 0;
  
  // These functions should be accessible from the presentation layer when 
  // the message is outbound, and from the Networking Actor subclass when 
  // the message is inbound.
  
  template< typename > friend class PresentationLayer;
  template< typename > friend class NetworkingActor;
  
  // The error reporting provides the information regarding the message type
  // so that a properly formatted error text can be provided if there is a 
  // mismatch between the presentation layer address and the payload of the 
  // polymorphic message. This string can also be used to identify the binary
  // message type for messages transmitted over the network, and it should 
  // work fine if the same compiler is used on all parts of the actor system.
  // Otherwise, one should use a manual typename for the message type 
  // identifier.
  
  virtual std::string GetProtocolTypeName( void ) const override
  { return boost::core::demangle( typeid( ProtocolPayload ).name() ); }
  
public:
  
  PolymorphicMessage()          = default;
  virtual ~PolymorphicMessage() = default;
};

}      // Name space Theron
#endif // THERON_POLYMORPHIC_MESSAGE
