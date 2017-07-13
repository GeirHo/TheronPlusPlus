# Theron++: An Actor Framework for C++

## Synopsis

The [Actor programming model](https://en.wikipedia.org/wiki/Actor_model) is a mathematical model of concurrent programming that is proven to be deadlock free as long as the actors only communicates with messages, and each actor only operates on its private memory. The actor model thereby avoids the complexity involved with implementing parallel and multi-threaded applications.

Theron++ is an actor framework compliant with the [Theron](http://www.theron-library.com/) Application Programming Interface (API), and it has been developed in line with the following objectives:
1. Do not implement what [C++11](https://isocpp.org/wiki/faq/cpp11) or later revisions offer as standard
2. Do not duplicate what the operating system offers
3. Respect the Theron API for the actors
4. Make the implementation as simple as possible

## Code Example

User defined actors are classes inheriting the Actor class. An actor must provide and register some functions to handle the messages it can receive. A message is just a user defined class that can be known both to the sending and the receiving actor. A working actor must therefore
1. Inherit the `Theron::Actor` class
2. Define one or more message handling functions
3. Register these message handlers in the constructor

The minimal user actor will follow the pattern of the following interface fragment

```C++
class MyActor : public Theron::Actor
{
private:
  void MessageTypeAHandler( const MessageTypeA & TheMessage, const Theron::Address SendingActor);
public:
  MyActor( const std::string & Name = std::string() )
  : Actor( Name )
  {
    RegisterHandler( this, &MyActor::MessageTypeAHandler );
  }
}
```
The supported Theron API is discussed below, and the [Hello World](https://github.com/GeirHo/TheronPlusPlus/blob/Release/Examples/HelloWorld.cpp) gives a more complete example on how to construct a small actor system. The [Examples folder](https://github.com/GeirHo/TheronPlusPlus/tree/Release/Examples) also contains a [makefile](https://github.com/GeirHo/TheronPlusPlus/blob/Release/Examples/makefile) to build the "Hello World" application.

## Motivation

Theron is the best actor model library for C++ with an elegant C++ API; arguably the _best_ API of the various actor model implementations existing for C++. However, the Theron library originated a decade ago as a library for embedded applications, and the support for the C++11 standard was integrated later mainly as standardised support for concepts already implemented in the library. Theron therefore supports user level memory allocation and scheduling of actors with pending messages on a pre-defined set of worker threads. This is functionality  typically ensured by the operating system, and supporting this in Theron makes the implementation of its library code unnecessary complicated and error prone. The long legacy also makes the well written code base relatively large and involved. The present Theron++ implementation came about after the author's several week long attempts trying to make a large Theron actor system work predictably under high load. 

## Installation

The code uses the `if constexpr` feature of C++17, and a compliant compiler is required.

The Theron++ framework is fully implemented in the Actor header and source file. The Presentation Layer header file is needed to compile the Actor source file, and the Serial Message header is needed by the Presentation Layer. There is no installation required, and these files can either be placed with the other source files of a project, or in a separate directory. 

All other files in this project are _utility actors_ supporting various functionality often implemented in actor systems. Four classes are related to the support of _transparent communication_ soon to be described in the project's wiki. The XMPP classes requires the [Swiften](http://swift.im/swiften.html) library.

## API Reference

The Actor header and source files fully support the following Theron class APIs:
* [Actor](http://docs.theron-library.com/6.00/classTheron_1_1Actor.html), at the exception of the depreciated `TrialSend` method
* [Receiver](http://docs.theron-library.com/6.00/classTheron_1_1Receiver.html), including the same behaviour of the `Wait()` function, which will only really block the calling thread if there are no unaccounted messages.
* [Address](http://docs.theron-library.com/6.00/classTheron_1_1Address.html), but the `GetFramework` has no meaning and returns the numerical ID of the actor referenced by the address.

Two obsolete classes are supported at the level of API to ensure that legacy Theron code will compile:
* 	**Framework** is a core component of Theron responsible for managing and executing actors. This is delegated to the operating system in Theron++ where the Framework is a simple actor capable of supporting the sending of messages. It is recommended not to use a Framework in applications created for Theron++, and it can safely be omitted from the constructors and functions where the Theron API requires a Framework. For instance, note the `Actor( Name )` constructor of the above code example which in Theron should have a Framework as first argument. The actor does provide a Theron compliant constructor, but it ignores the Framework and delegates the actor initialisation to the `Actor( Name )` constructor.
* 	**Endpoint** is responsible for network communication in Theron. This is replaced by the four actors supporting _transparent communication_: 
	1. **Presentation Layer** responsible for serialising and de-serialising messages
	2. **Session Layer** responsible for mapping a local actor ID to a remote actor address
	3. **Network Layer** implementing the network protocol in use
	4. **Network End Point** to ensure that the three other actors are correctly bound together, and started and stopped consistently.

	Thus, no Endpoint should be used as the current implementation has no functionality. Legacy applications using the Endpoint for communication are probably outdated anyway since Theron's Endpoint is based on Crossroads.io (XS.h) which has been [superseded](http://stackoverflow.com/questions/13494033/zeromq-vs-crossroads-i-o) by the [nanomsg](https://github.com/nanomsg/nanomsg) library. Theron++ does support [XMPP](https://xmpp.org/) and has tested interoperability with the Python [SPADE](https://github.com/javipalanca/spade) mult-agent environment. Support for [ZeroMQ](http://zeromq.org/) is currently being implemented.

The Theron classes supporting the user defined memory allocation (**AllocatorManager**, **DefaultAllocator**, and **IAllocator** ) are not supported since the standard memory allocation is used in Theron++. Theron's **Catcher** class is made redundant by implementing the Receiver as a subclass of Actor, and the Catcher's message management can consequently be implemented as normal message handlers on a subclass of the Receiver.

## Tests

The Hello World example can be built using the provided makefile `make HelloWorld`. Theron legacy applications can be compiled by replacing the `Theron.h`file with the `Actor.hpp`file and replace the Theron library at linking with the compiled `Actor.cpp`, i.e. `Actor.o`as the last project object code file.

## Contributors

The set of utility actors and support for different useful communication protocols will be expanded over time as the Theron++ library is used in various projects. Contributions from other users are most welcome!

## License

All source code is released under the GNU Lesser General Public License Version 3 
([LGPLv3](https://www.gnu.org/licenses/lgpl.html))
