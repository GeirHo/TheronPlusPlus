/*==============================================================================
Active Message Queue Interface

The Active Message Queue (AMQ) [1] is a server based messaging system where
various clients can exchange messages via a server (message broker). The 
interface to the AMQ server (broker) is based on the Qpid Proton [2] 
Application Programming Interface (API) [3]. The Network Layer encapsulates 
all Proton activities and is the handler for all Proton call-backs. 

The Network Layer represents externally the Actors on an endpoint. As an Actor 
does not know where another Actor is located, Actor endpoint locations will be 
resolved for distributed Actor systems using a protocol among the Network 
Layers. There is a dedicated AMQ topic 'TheronPlusPlus' used for this purpose. 
When a local Actor sends a message that can not be delivered locally, the 
message will be cached first at the Session Layer sending a resolution request 
to the Network Layer. An endpoint resolution request will be posted on the 
'TheronPlusPlus' topic. The endpoint hosting the searched Actor 
will then set up a Subscription for the requesting Actor topic knowing that 
a message will be coming soon, and respond back on the 'TheronPlusPlus' topic
the global address of the requested Actor. All endpoints will cache all 
discovered global addresses to avoid resolving an address again. 

When an Actor closes its address will be broadcast so that all remote endpoints 
having subscriptions for the closing Actor will remove these. Another broadcast
will follow when an endpoint closes to indicate that all Actors on this endpoint
are unavailable and all publishers for these actors should be removed. 

This is the basic Actor-to-Actor protocol. However it is also possible for an 
Actor to request a topic for just publishing information without knowing if
there is any remote Actor subscribing to these messages. On the same note, an 
Actor can also subscribe to any topic, even if the publisher for that topic is 
not an Actor. One only needs to obey the use of the AMQ headers:
  - The 'to' field is the 'topic', which could be an Actor address
  - The 'reply-to' field is the Actor or topic sending the message.
  - The 'subject' is the command only on the 'TheronPlusPlus' control topic
  - The 'body' is defined only for the 'TheronPlusPlus' control topic
  
The main purpose of the Session Layer is to maintain a bidirectional mapping 
between the global address of an Actor and its local address since the global 
address depends on the underlying Network Layer technology. As described above,
the Session Layer will initiate a global address resolution if it does not have 
the mapping entry for the requested global Actor. The second task of the 
Session Layer is to keep track of 'sessions' between local Actors and external 
actors. These sessions are opened when any of the two Actors sends the first 
message, and definitely closed if any of the two actors closes, or if the 
endpoint hosting remote actors closes forcing all sessions with these actors to
close.

The Presentation Layer is mapping messages from a binary message used among 
Actors on this endpoint to a network message that can be transmitted across 
the network. 

The EndPoint Actor is encapsulating these three layers for a given network 
protocol to ensure that they all work on compatible address and message formats.
Hence, in an application only the AMQ End Point Actor needs to be started, and
the following parameters must be given to its constructor:
1. The name of the endpoint. All external addresses will contain this 
   endpoint name in the global address, see the AMQ Message header defining 
   the global address format.
2. The URL of the AMQ message Broker to be used
3. The network port of the AMQ Broker to be used
4. The name string of the Network Layer Actor
5. The name string of the Session Layer Actor
6. The name string of the Presentation Layer
7. The Qpid Proton Connection Option class. This is given as a class since it 
   contains the parameters needed to connect to the server, like the user name 
   and password, as well as settings for encrypting the communication.
8. The Qpid Proton Message Properties class. These message properties will be 
   added to all outbound messages. Hence if one wants to use specific message 
   properties they must not be set in this default class as the default will
   override the specific properties set in a message if they are given in the 
   property class passed to the AMQ Endpoint class. The 'to' and 'reply-to'
   fields will always be set as indicated above.

References:
[1] http://activemq.apache.org/
[2] https://qpid.apache.org/proton/index.html
[3] https://qpid.apache.org/releases/qpid-proton-0.39.0/proton/cpp/api/index.html

Author and Copyright: Geir Horn, University of Oslo
License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
==============================================================================*/

#ifndef THERON_AMQ
#define THERON_AMQ

#endif 
