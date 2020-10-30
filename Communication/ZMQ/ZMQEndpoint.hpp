/*==============================================================================
Zero Message Queue (ZMQ) Network Endpoint

The Zero Message Queue (ZMQ) [1] is a peer-to-peer network communication layer
providing sockets for connecting network endpoints and allowing an actor system
to be distributed over multiple nodes. Hence, it should allow actors to exchange
messages based on unique actor names. Under the principle of transparent
communication, it should not be necessary to rewrite any actor code, and the
actors need not to know the endpoint executing the actor. The only requirements
on the actor code is that the messages that are exchanged with actors
potentially on remote remote nodes must be derived from the Serial Message
class to support serialisation. Consequently, the actor should be derived from
the de-serialising class to participate in the remote exchange.

A message for an actor whose name cannot be resolved to an actor executing on
this endpoint will first be delivered to the Presentation Layer for
serialisation, and then passed on to the Session Layer that translates the
actor address to an address that can be routed externally, and finally the
Network Layer sends the message to a remote endpoint. This header file and
the corresponding source file implements the Network Layer based on the ZMQ.

The ZMQ library is written in C, and there are a few high level bindings for
C++. The official binding cppzmq [2] is C-style code really not using the
features of modern C++. The azmq [3] is aimed for implementations combining ZMQ
communication with the Boost Asio [4] library. There were thus only two good
candidates for the use with this implementation: CpperoMQ [5] and zmqpp [6].
CpperoMQ is an elegantly written header only library and targets sending of
multi-part messages by overloading virtual functions to encode or decode the
message parts. Unfortunately, CpperoMQ seems not to be updated for
ZMQ version 4.x. The zmqpp library requires installation, but it has been
updated with support for ZMQ version 4.x and does support multi-part messages
through overloaded stream operators. It also provides a reactor to monitor
activity on multiple sockets which can be dynamically added, in contrast with
CpperoMQ that only supports polling on sockets defined at compile time. The
zmqpp is therefore selected as the basis for this implementation, and it must
be installed and the files can be included from

-I $(zmqppdir)/src/zmqpp

with the following libraries to be given for the linker (in this order)

-lzmqpp -lzmq -lboost_system

It should also be noted that the default installation of zmqpp installs the
shared library It should be noted that the under /usr/local/lib and so that
directory must be used by the linker. There are many ways to do this, please
see Bob Plankers' excellent discussion [7].

References:

[1] http://zeromq.org/
[2] https://github.com/zeromq/cppzmq
[3] https://github.com/zeromq/azmq
[4] http://www.boost.org/doc/libs/1_64_0/doc/html/boost_asio.html
[5] https://github.com/jship/CpperoMQ
[6] https://github.com/zeromq/zmqpp
[7] https://lonesysadmin.net/2013/02/22/error-while-loading-shared-libraries-cannot-open-shared-object-file/

[10] https://en.wikipedia.org/wiki/IPv6

[15] https://rfc.zeromq.org/spec:29/PUBSUB/
[16] http://hintjens.com/blog:37

Author and Copyright: Geir Horn, University of Oslo, 2017-2019
License: LGPL 3.0
==============================================================================*/
