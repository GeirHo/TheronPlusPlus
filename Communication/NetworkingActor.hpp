/*=============================================================================
Networking actor

When a message arrives from the network it comes as polymorphic message 
that must be decoded to the corresponding binary message. This decoding is 
supposed to be handled in one place: by the message itself. The approach is 
therefore to go message type by message type and try to decode the payload 
to the given message type, and if the decoding fails, one should move to the 
next message type. If the decoding was successful, the message constructed 
can be send to the actor's normal message handler for the binary message.

The ideal situation is that a class only register the message handlers as
normal, and then overloading and polymorphism ensures that this message is
registered as a message that supports network sending. This can be detected
by overloading the actor's Register Handler checking if the message is
derived from the polymorphic message class, and register the message if 
this holds.

The first issue relates to the difference between "instance" and "type" of
the actor. Each instance will register its message handlers. Since the
handlers are the same for all instances of a type and only the 'this' pointer
decides which actor instance execute the handler, Hence, the list of
polymorphic messages could be shared by all instances of that actor type.
This would imply some actor type specific static structure remembering the
message types.

The second issue relates to inheritance. Consider the following situation:
Actor C is derived from B which is derived from A. Each of the actors in
the family line declares some polymorphic messages with corresponding
handlers. When a polymorphic message arrives for an instance of actor C, one
could use its type ID to find the structure for its serialised messages,
but this will be different from the structure for the actor types B and A.
This implies that the structures must be chained in some way.

These issues were solved in the first release by defining a static class
holding the various message types. This class was derived along the
inheritance tree, adding the polymorphic message types supported at each
level. Finally, there was a polymorphic function returning the pointer
to the static structure that could be called on an instance to get the
message types supported by the actor type of that instance.

The downside of this approach was that each class supporting polymorphic
messages had to extend this static structure. This is functionality external
to the actor definition, and the developer of a networking actor needs
to be aware of this code pattern and apply it to the derived actors. This
is error prone and a better approach was needed.

The second issue can be solved by observing that the compiler will enforce
the initialisation of the classes in the inheritance tree from the base
and up. This implies that by checking the type ID of the class registering
the message, one will be able to first build the list of messages supported
by actor type A, then actor type B, and finally actor type C. Then one could
make each set of messages refer back to the previous actor type set. When a
polymorphic message arrives for an instance of actor type C, it will first try
to construct the messages for actor type C, then for actor type B, and
finally for actor type A.

The first issue could then be solved by the networking actor having
a static structure mapping all the message types to their corresponding
sets of serialised message types. This would allow the messages to be
registered by type, and only once per actor type, and all instances of
serialising actors would share this database.

However, the actors execute in separate threads, and therefore if two or more
actors have received polymorphic messages. they would both need to access
the registered serial message types in this database. Hence there should be
a lock (mutex) serialising the access to the database. Furthermore, the lock
must be kept until a message de-serialisation has been completed to success
or failure. This could severely hamper application performance.

Alternatively, each de-serialising actor instance could have its own map of
messages, This would duplicate the the database of serial messages supported
by a particular actor for each instance. Access would be simpler in this
case since each actor could decode the incoming messages in parallel
and independent of the activities of the other actors in the system. Hence,
it is the classical trade-off between memory use and performance.

The current implementation emphasises performance, and the networking actor
class defines its own database of messages supporting serialisation, and
this will therefore be unique to each instance of an actor supporting
serialisation. Hopefully, the number of messages supported by an actor is
not very large, and not too much memory will be wasted by this approach.

Author and Copyright: Geir Horn, University of Oslo
License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
=============================================================================*/

#ifndef THERON_NETWORKING_ACTOR
#define THERON_NETWORKING_ACTOR

#include <functional>														// Functional programming
#include <map>																	// Standard maps
#include <source_location>											// For error reporting
#include <sstream> 														  // Formatted errors 
#include <stdexcept>                            // Standard exceptions
#include <algorithm>														// Standard algorithms
#include <concepts>                             // For template parameters

#include "Actor.hpp"							              // The Theron++ Actor Framework
#include "Utility/StandardFallbackHandler.hpp"  // Catch unhanded messages

#include "Communication/NetworkEndpoint.hpp"		// Network communication
#include "Communication/SessionLayer.hpp"				// Address mapping

#include "Communication/PolymorphicMessage.hpp" // The protocol message base

//Debuging

#include "Utility/ConsolePrint.hpp"
#include <boost/core/demangle.hpp>              // To print readable types

namespace Theron {
  
template< class ProtocolPayload >
class NetworkingActor : 
 virtual public Actor,
 virtual public StandardFallbackHandler
{
protected:

  using PayloadType = ProtocolPayload;

private:

  // When a polymorphic message arrives it is given to a function that constructs
  // the message, decode the message, and if successful, it will forward
  // the message to the normal message handler.

  using MessageCreator = std::function< bool(const ProtocolPayload &,
                                             const Address & ) >;

  // The polymorphic message types are kept in a standard map since it
  // will be sequentially traversed when a polymorphic message comes in.

  std::map< std::type_index, MessageCreator > MessageTypes;

  // Since Theron++ allows multiple handlers to be registered for the same
  // it is necessary to keep a count of handlers. This in order to be able
  // to remove the message type once the last handler function is de-registered.
  // The normal way would have been to bundle this with the value type in the
  // above map, but since the message types are parsed every time a message
  // arrives while the handler count is only updated when a handler is
  // registered, the counters are kept in a parallel map.

  std::map< std::type_index, unsigned int > HandlerCount;

  // Messages are registered with a message specific creator function that
  // will forward the message to the right message handler provided that
  // the message was correctly decoded. Note that there is no test to
  // check that the message is polymorphic since this test is best done
  // prior to invoking this function.

  template< class MessageType >
  void RegisterMessageType( void )
  {
    // The function to construct this message is defined as a lambda passed
    // and stored in the map for this type of messages.

    auto InsertResult =
    MessageTypes.emplace( typeid( MessageType ),
                 [this]( const ProtocolPayload & Payload,
                         const Address & Sender )->bool
       {
          MessageType BinaryMessage;

          // There is a small issue with access. The Networking Actor is a
          // friend of the polymorphic message, but in general it cannot access
          // protected members of derived message types. Hence the function to
          // de-serialise the message must be called on a polymorphic message 
          // base class, which then uses the implementation of the derived class.

          PolymorphicMessage< ProtocolPayload > * NewMessage( &BinaryMessage );

          if ( NewMessage->Initialize( Payload ) )
          {
            Send( BinaryMessage, Sender, GetAddress() );
            return true;
          }
          else
            return false;
       } // End of lambda
    );   // End of map emplace

    // If a new record was created for this message type, the hander count
    // should be initialised to unity, otherwise it should just be increased.

    if ( InsertResult.second )
      HandlerCount[ typeid( MessageType ) ] = 1;
    else
      HandlerCount[ typeid( MessageType ) ]++;
  }

  // Processing an incoming polymorphic message is then simply trying to 
  // construct the messages until one message type is successfully constructed. 
  // If no messages are registered or if the end of the message type map 
  // is reached with no successful construction, a runtime error is thrown.

  void PolymorphicMessageHandler ( const ProtocolPayload  & Payload,
                                   const Theron::Address Sender )
  {
    if ( MessageTypes.empty() )
    {
      std::ostringstream ErrorMessage;
      std::source_location Location = std::source_location::current();

      ErrorMessage << Location.file_name() << " on line " 
                   << Location.line()<< " : " << "Actor " 
                   << GetAddress().AsString() << "in handler " 
                   << Location.function_name() << " received "
                   << " a polymorphic message from " << Sender.AsString()
                   << " but no polymorphic message type is registered";

      throw std::runtime_error( ErrorMessage.str() );
    }
    else
    {
      // If the payload does not correspond to any of the available messages,
      // an exception will be thrown as this situation should not occur.

      if( std::ranges::count_if( std::ranges::views::values( MessageTypes ), 
              [&]( auto & InitialiserFunction)->bool{ 
              return InitialiserFunction( Payload, Sender ); })
          == 0 )
      {
        std::ostringstream ErrorMessage;
        std::source_location Location = std::source_location::current();

        ErrorMessage << Location.file_name() << " on line " 
                    << Location.line()<< " : " << "Actor " 
                    << GetAddress().AsString() << "in handler " 
                    << Location.function_name() << " received"
                    << " a message from " << Sender.AsString()
                    << " which did not initialize any known message";

        throw std::invalid_argument( ErrorMessage.str() );
      };
    }
  }

  // Given that there must be a message handler for all messages an actor can
  // receive, the different message types can be captured when the message
  // handler is registered. Consequently, the actor's register handler is
  // overloaded with a version first registering the message type before
  // forwarding the registration to the normal message handler registration.
  //
  // The message type test is known at compile time and the optimiser should
  // remove the if statement if the test fails leaving this as a simple
  // instantiation of the actor's register handler.

protected: 

  template< class ActorType,  class MessageType >
  requires std::derived_from< ActorType, Actor > && 
           std::default_initializable< MessageType >
  inline bool RegisterHandler( ActorType  * const TheActor,
               void (ActorType::* TheHandler)(	const MessageType & TheMessage,
                                                const Address From ) )
  {
    if constexpr ( std::is_base_of< PolymorphicMessage< ProtocolPayload >, 
                                    MessageType>::value )
      RegisterMessageType< MessageType >();

    return Actor::RegisterHandler( TheActor, TheHandler );
  }

  // It is necessary also to implement the remove handler, which will first
  // ask the actor the remove the corresponding handler, and if the actor did
  // remove a handler function, then the count of handlers for this message
  // type will be decremented, and if it was the last handler for this message
  // type, the type will be erased from the registry.

  template< class ActorType, class MessageType >
  inline bool DeregisterHandler( ActorType  * const HandlingActor,
                  void (ActorType::* TheHandler)(const MessageType & TheMessage,
                                                 const Address From ) )
  {
    bool HandlerRemoved = Actor::DeregisterHandler( HandlingActor, TheHandler );

    if ( HandlerRemoved )
      if ( --( HandlerCount[ typeid ( MessageType ) ] ) == 0 )
         MessageTypes.erase( typeid( MessageType ) );

    return HandlerRemoved;
  }

private:

  bool NetworkConnected;

protected:

  // A small helper function allows derived classes to check if the network is
  // connected.

  inline bool HasNetwork( void )
  { return NetworkConnected; }

  // The Networking Actor has to sign in with the Session Layer server,
  // and sign out of the session layer. The standard way would be to set up a
  // virtual function to return the session layer of the used transport layer
  // protocol. However, this will not be initialized when the networking
  // actor starts, and the virtual function table is gone when the class
  // is destructed. The solution is therefore to store the session layer
  // address upon construction.

private:

  Address SessionLayerAddress;

  // The shut down message is sent from the Session Layer to all registered
  // networking actors when the session layer is asked to shut down. This
  // message indicates that the node is shutting down, at least that the
  // network interface is going down and that messages to remote actors will
  // not be sent. The handler is therefore virtual so that derived classes
  // can take appropriate action. The default action is just to send a de-
  // registration message back to the session layer.

  virtual void Stop( const Network::ShutDown & StopMessage,
                     const Address Sender )
  {
    if ( SessionLayerAddress )
      Send( SessionLayerMessages::RemoveActorCommand(), SessionLayerAddress );

    NetworkConnected = false;
  }

  // External address mapping is done by the Session Layer actor, and if actors
  // on other endpoints are to address this actor, the Session Layer on this
  // endpoint must tell the other session layers that the actor is on this
  // endpoint. An actor supporting external communication must be derived
  // from this the networking actor type, and it is therefore sufficient
  // that the session layer registration is automatically managed by the
  // constructor of the networking actor.
  //
  // However, this implicitly puts a constraint on the order of actor creation
  // since the session layer actor (and the Network End Point) must be
  // instantiated before any networking actors. It could possibly be
  // application scenarios where this would be impossible. Furthermore, the
  // idea of transparent communication is that application code should not be
  // changed when the actor system is distributed across several computing
  // nodes. It is therefore very likely that an application is developed and
  // tested without any network endpoint class and without any session layer
  // class, and that these classes will be added later when the application's
  // behaviour has been verified.
  //
  // These considerations indicate that it is not possible to consider it an
  // error if the session layer does not exist at the time a networking
  // actor is constructed. Hence, no exception should be thrown if there is no
  // session layer actor available at construction. Furthermore, the
  // application code should be allowed to do the registration at some later
  // convenient point, instead of doing it during the actor construction.
  //
  // Consequently, a special registration function is provided. Note that this
  // is private to ensure that the registration is only done when a session 
  // layer address is set.

  inline bool RegisterWithSessionLayer( void )
  {
    if ( SessionLayerAddress )
    {
      Send( SessionLayerMessages::RegisterActorCommand(), SessionLayerAddress	);
      NetworkConnected = true;
      return true;
    }
    else
      return false;
  }

  // To support these scenarios where late registration must be allowed, there
  // is a helper function setting the session layer server address. It makes
  // sure to de-register if the session layer has already been set.

protected:

  inline void SetSessionLayerAddress( const Address NewAddress )
  {
    if( SessionLayerAddress )
      Stop( Network::ShutDown(), Address::Null() );

    SessionLayerAddress = NewAddress;
    RegisterWithSessionLayer();
  }

  // There is also a trivial function to get the stored session layer address
  // if derived Actors would need to use this directly.

  inline Address GetSessionLayerAddress( void )
  { return SessionLayerAddress; }

  // The constructor is defined in the code file because it will register the
  // actor with the session layer to create an external presence for this actor.
  // The philosophy is that in order to be able to participate in network
  // endpoint external communication, an actor must be derived from the de-
  // serialising actor, and therefore the registration of this actor with the
  // session layer should be done only by the de-serialising constructor. In
  // the same way, the de-serialising destructor will make sure to de-register
  // this actor when it terminates.
  //
  // A subtle point is that the use of the Actor base class registration 
  // function for the message handlers to avoid that the polymorphic message 
  // is registered as a polymorphic message, which would make the polymorphic
  // message handler to be called again and again... an infinite message loop!

public:

  explicit NetworkingActor( const std::string & name,
                            const Address TheSessionLayer )
  : Actor( name ),
    StandardFallbackHandler( GetAddress().AsString() ),
    NetworkConnected( false ), SessionLayerAddress( TheSessionLayer )
  {
    Actor::RegisterHandler( this, &NetworkingActor::PolymorphicMessageHandler );
    Actor::RegisterHandler( this, &NetworkingActor::Stop );

    if ( SessionLayerAddress )
      RegisterWithSessionLayer();
  }

  // The normal behaviour is to start the network endpoint before creating the
  // networking actors, and so the normal constructor will resolve the Session
  // Layer address from the endpoint. If that is not possible, and one really
  // would need the delayed session layer address registration, the previous 
  // constructor should be used with the session layer explicityly set to 
  // Address::Null()

  NetworkingActor( const std::string & name = std::string() )
  : NetworkingActor( name, Network::GetAddress( Network::Layer::Session ) )
  {}

  // And we need a virtual destructor to ensure that everything will be
  // cleaned correctly. It should also inform the session layer actor
  // on this endpoint that this actor will no longer be available and that
  // no messages for this actor should be accepted.

  virtual ~NetworkingActor()
  {
    if ( NetworkConnected )
      Stop( Network::ShutDown(), Address::Null() );
  }
};


}				// End name space Theron
#endif 	// THERON_DESERIALIZING_ACTOR
