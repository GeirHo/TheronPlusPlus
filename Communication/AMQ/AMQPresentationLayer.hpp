/*==============================================================================
Active Message Queue (AMQ): Presentation layer

The presentation layer is responsible for converting binary messages sent from
actors on this endpoint to messages that can be transmitted on the network to
actors on remote endpoints. This can be seen as a generalised serialisation 
process, and in the case of serialisation the resulting payload is a string. 
However, it can be whatever format supported by the link layer protocol 
implementation encapsulated by the Network Layer. The payload for the AMQ
protocol will be a pointer to a Qpid Proton message. A pointer is used should 
the same message be sent to multiple receivers as it avoids copying the 
full message object.

The way a message is converted to a Qpid Proton message depends on the 
information in the message. It is recommended that the advanced features of
structured messages will be used [1]. In the same way, inbound messages will 
be sent as pointers to the Qpid Proton message to be unpacked by the Networking
Actor base class of all actors to communicate with remote actors. This unpacking
is again specific to the message content, and must be implemented in the 
message type object. 

The result is that the presentation layer in this case is nothing more than 
a specialisation of the generic link layer for the AMQ message payload.

References:
[1] https://qpid.apache.org/releases/qpid-proton-0.39.0/proton/cpp/api/types_page.html

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
==============================================================================*/

#ifndef THERON_AMQ_PRESENTATION_LAYER
#define THERON_AMQ_PRESENTATION_LAYER

// Theron++ actor framework headers

#include "Communication/PresentationLayer.hpp"

// AMQ specific headers

#include "proton/message.hpp"
#include "Communication/AMQ/AMQMessage.hpp"

namespace Theron::AMQ
{

using PresentationLayer = Theron::PresentationLayer< 
                          typename AMQ::Message::PayloadType >;

}      // Namespace Theron::AMQ
#endif // THERON_AMQ_PRESENTATION_LAYER