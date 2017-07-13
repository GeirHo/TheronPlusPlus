/*=============================================================================
  Presentation Layer
    
  A message must be a Serial Message for it to be able to be sent across the 
  network. In terms of this framework it means that the message must inherit 
  the Serial Message base class, and implement the virtual function
  std::string Serialize() from that base class. It will be called by the 
  Presentation Layer to pack up the message before it is transmitted
    
  This class is a part of the revised and extended Theron mechanism for external
  communication. The Presentation Layer is a proxy for the remote agent, and 
  it receives messages to and from the network. Consider the following small 
  example: There are three actor of the same type A, B, and C exchanging data 
  in a complex message format (class). A and B are on the same endpoint, 
  i.e. network node, whereas C runs remotely. A will use the same send request 
  irrespective of the destination actor. In the case the receiver is B, then 
  the message will be put in the message queue of B. However, if the receiver 
  is C, then it is detected by the Theron external communication extensions 
  that actor C is on a remote endpoint, and the message will be delivered to 
  the inbound queue for the Presentation Layer server, which is an actor. The 
  Presentation Layer server will then call the Serialize() method on the message 
  to obtain the string representation of the message to send to the remote 
  actor C.
    
  After packing the message up as a string, the string will be forwarded to the
  Session Layer server which will embed the message as the payload of a message
  in the right protocol, before the Session Layer delivers the message to 
  the Network Layer for actual transmission.
  
  The reverse process is more complex because the only information available 
  to the Session Layer is the actor ID of the remote sender, the actor ID of the 
  local actor to receive the message, and a string representing the message 
  content. The two actors involved can exchange many different types of 
  messages, and there is no way for the Presentation Layer automatically to 
  deduce which binary message the string should be converted into. The user must 
  therefore provide a message handler capable of receiving a string message 
  on each actor class that receives serialized messages. This method must
  convert the string it receives back to the right binary message format and 
  call the actor's message handler for this binary message.
  
  In order to allow the actor to have a string handler for normal peer to peer
  communication, the special class SerialMessage::Payload is used for the 
  message format so that the actor can distinguish between strings that that 
  contains serialised binary structures and normal strings. The actual 
  initialisation of a binary message from a string should be done by the 
  Deserialize method that must be implemented for each message that should be 
  transferable over the network.
  
  To continue the above example: When a message arrives from C, the Presentation 
  Layer will receive a serial message with C as the sender and, say, B as the 
  receiver. The payload will be stored as a Payload and then forwarded
  to as if it comes from C, and B's handler for Payloads will receive 
  the message, convert it to the right binary format, and resend the message to
  B's handler for the given message binary message type. 
   
  REVISION: This file is NOT compatible with standard Theron - the new actor 
            implementation of Theron++ MUST be used.
 
  Author: Geir Horn, University of Oslo, 2015 - 2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef THERON_PRESENTATION_LAYER
#define THERON_PRESENTATION_LAYER

#include <string>
#include <sstream>
#include <algorithm>
#include <functional>
#include <map>
#include <iterator>
#include <type_traits>
#include <typeinfo>
#include <typeindex>
#include <stdexcept>

#include "Actor.hpp"
#include "StandardFallbackHandler.hpp"
#include "NetworkEndPoint.hpp"
#include "SerialMessage.hpp"

// The Presentation Layer is defined to be a part of the Theron name space 

namespace Theron
{

/*=============================================================================

 Presentation layer

=============================================================================*/
//
// The Presentation Layer is itself an actor that will exchange messages with 
// other actors, and with the Session Layer server. 
  
class PresentationLayer : public virtual Actor,
													public virtual StandardFallbackHandler
{
public:

  // --------------------------------------------------------------------------
  // Remote message format
  // --------------------------------------------------------------------------
  //
	// The general mechanism of serialisation is discussed above. Transparent
	// communication means in this context that the actors should be identical 
	// whether they are at the same network endpoint or on different endpoints
	// (or nodes) as long as the message sent supports serialisation. This the 
	// serialisation depends on the actual data fields and structure of the 
	// message to be transmitted. Fundamentally, a serialised message only needs 
	// to contain the sender's address, the receiver's address and a string 
	// representing the serialised message.
	//
  // The result of the serialisation and what is received from the remote actor
  // is a thus a serialised message, which is a class combining the two 
	// involved addresses and the payload string. This class is also what will 
	// be forwarded to the protocol engine to be sent out on the network.
  
  class RemoteMessage
  {
  private:
    
    Address From, To;
    SerialMessage::Payload Message;
    
  public:
    
    RemoteMessage( const Address & TheSender, const Address & TheReceiver, 
								   const SerialMessage::Payload & ThePayload )
    : From( TheSender ), To( TheReceiver ), Message( ThePayload )
    {};
    
    // Interface functions 
    
    inline Address GetSender( void ) const
    {
      return From;
    }
    
    inline Address GetReceiver( void ) const
    {
      return To;
    }
    
    inline SerialMessage::Payload GetPayload( void ) const
    {
      return Message;
    }

  };
  
  // --------------------------------------------------------------------------
  // Serialisation and de-serialisation
  // --------------------------------------------------------------------------
  // 
  // The serialised message will be forwarded to the Session Layer server for 
  // being encapsulated and sent to the remote actor.
  
protected:
	
  Address SessionServer;

  // A fundamental issue is that Theron's message handlers do not specify the 
	// receiver of a message because it is unnecessary since the receiver is and 
	// actor on the local endpoint. However, for transparent communication it 
	// is necessary to intercept the message if it is destined for an actor on 
	// a remote endpoint. Then the real receiver's address is needed to construct 
	// correctly the serialised message. In other words, the Presentation Layer 
	// cannot define a simple message hander, since the "To" address would be 
	// lost.
	// 
	// The option is to modify the message enqueue function and intercept the 
	// message before it is queued for local handling since the generic message
	// contains information about both the sender and the receiver. 
	// 
	// Two cases must be considered: The one where a message is outbound for a 
	// remote endpoint, and the case where the message is inbound coming from an
	// actor on a remote endpoint and addressed to a local actor. In the outbound
	// case the message should be Serializeable, and in the inbound case it
	// it should be a Serialised Message. 
	//
	// The implementation is therefore based on the Run Time Type Information
	// (RTTI) and the ability to convert the generic message to a message of the 
	// expected type. Invalid argument exceptions will be created if the message 
	// given is not of the expected type. Note that the message will not be 
	// enqueued for processing with this actor. 
	
	virtual
	bool EnqueueMessage( const std::shared_ptr< GenericMessage > & TheMessage )
	{
		if ( TheMessage->To == GetAddress() )
		{
			// Note that the test is made to see if the message is from the Session 
			// Layer to this Presentation Layer as this implies an incoming message 
			// to an actor on this endpoint that should be of type Remote Message.
			// The real sending actor the real destination actor are encoded in the 
			// remote message.
			
			auto InboundMessage = 
					 std::dynamic_pointer_cast< Message< RemoteMessage > >( TheMessage );
					 
		  // If the message conversion was successful, then this can be forwarded
		  // to the local destination actor as if it was sent from the remote 
		  // sender.
					 
		  if ( InboundMessage )
				Send( InboundMessage->TheMessage->GetPayload(), 
							InboundMessage->TheMessage->GetSender(), 
							InboundMessage->TheMessage->GetReceiver() );
			else
			{
				std::ostringstream ErrorMessage;
				
				ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
										 << "Inbound message to the Presentation Layer from "
										 << TheMessage->From.AsString() << " with receiver " 
										 << TheMessage->To.AsString()
										 << " is not a Serial Message";
				
				throw std::invalid_argument( ErrorMessage.str() );
			}
		}
		else
		{
			// The outbound message should in this case support serialisation, and 
			// the payload is created first.
			
			SerialMessage * OutboundMessage( TheMessage->GetSerialMessagePointer() );
			
		  // A valid message will in this case be forwarded as a remote message 
		  // to the Session Layer server.
			
			if ( OutboundMessage != nullptr )
				Send( RemoteMessage( TheMessage->From, TheMessage->To, 
														 OutboundMessage->Serialize() ), 
							SessionServer );
			else
			{
				std::ostringstream ErrorMessage;
				
				ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
										 << "Outbound message to the Presentation Layer from "
										 << TheMessage->From.AsString() << " with receiver "
										 << TheMessage->To.AsString() 
										 << " does not support serialisation";
				
				throw std::invalid_argument( ErrorMessage.str() );
			}
		}
		
		// If this point is reached, then the message handling must have been 
		// successful (otherwise and exception would have resulted), and it can be
		// confirmed as successful.
		
		return true;
	}
  
public:
  
  // --------------------------------------------------------------------------
  // Constructor
  // --------------------------------------------------------------------------

  // The Presentation Layer class sends and receives messages from the Session
  // Layer class, and vice versa. This makes it impossible that both classes can 
  //K receive the other class' address as an argument to their constructors. The 
  // binding must be done explicitly once both classes have been created, and 
  // there is a support function to register the address of the protocol engine
  // in the corresponding variable.
  
  inline void SetSessionLayerAddress( const Address & SessionServerActor )
  {
    SessionServer = SessionServerActor;
  }
  
  // The constructor registers the handler for the incoming messages and the 
  // default handler. The Session Layer server address is initialised with the 
  // default address of the Session Layer. This is possible since a Theron 
  // Address does not check that the actor exists when it is constructed on 
  // a string. The check is only done when the fist message is sent to this 
  // address. Hence, as long as the default names are used for the actors,
  // this no further initialisation is needed.
  
  PresentationLayer( const std::string ServerName = "PresentationLayer"  ) 
  : Actor( ServerName ),
    StandardFallbackHandler( Actor::GetAddress().AsString() ),
    SessionServer()
  {
		Actor::SetPresentationLayerServer( this );
  }
    
  // The compatibility constructor requires a pointer to the network endpoint
  
  PresentationLayer( NetworkEndPoint * TheHost,
								     const std::string ServerName = "PresentationLayer"  ) 
  : Actor( ServerName ),
    StandardFallbackHandler( TheHost->GetFramework(), ServerName.data() ),
    SessionServer()
  {
		Actor::SetPresentationLayerServer( this );
  }
};


/*=============================================================================

 De-serialising actor

=============================================================================*/
//
// When a message arrives from the network it comes as a string of characters
// that must be de-serialised to the corresponding binary message. However, 
// without decoding the string it is not possible to know which serialised 
// message the string corresponds to. This decoding is supposed to be handled
// in one place: by the message itself. The approach is therefore to go 
// message by message and try to de-serialise the string to the given message 
// type, and if it fails, one should move to the next message type. If the 
// de-serialisation was successful, the message constructed can be send to 
// the actor's normal message handler for the binary message.
//
// The ideal situation is that a class only register the message handlers as 
// normal, and then overloading and polymorphism ensures that this message is 
// registered as a message that supports serial sending. This can be detected
// by overloading the actor's Register Handler checking if the message is 
// derived from the Serializeable class, and register the message if this holds.
//
// The first issue relates to the difference between "instance" and "type" of 
// the actor. Each instance will register its message handlers. Since the 
// handlers are the same for all instances of a type and only the 'this' pointer
// decides which actor instance execute the handler, Hence, the list of 
// Serializeable messages could be shared by all instances of that actor type.
// This would imply some actor type specific static structure remembering the 
// message types.
//
// The second issue relates to inheritance. Consider the following situation:
// Actor C is derived from B which is derived from A. Each of the actors in 
// the family line declares some Serializeable messages with corresponding
// handlers. When a serialised payload arrives for an instance of actor C, one 
// could use its type ID to find the structure for its serialised messages, 
// but this will be different from the structure for the actor types B and A.
// This implies that the structures must be chained in some way.
//
// These issues were solved in the first release by defining a static class 
// holding the various message types. This class was derived along the 
// inheritance tree, adding the serialised message types supported at each 
// level. Finally, there was a polymorphic function returning the pointer 
// to the static structure that could be called on an instance to get the 
// message types supported by the actor type of that instance.
//
// The downside of this approach was that each class supporting serialised 
// messages has extend this static structure. This is functionality external
// to the actor definition, and the developer of a serialising actor needs 
// to be aware of this code pattern and apply it to the derived actors. This 
// is error prone and a better approach was needed.
//
// The second issue can be solved by observing that the compiler will enforce 
// the initialisation of the classes in the inheritance tree from the base 
// and up. This implies that by checking the type ID of the class registering 
// the message, one will be able to first build the list of messages supported
// by actor type A, then actor type B, and finally actor type C. Then one could
// make each set of messages refer back to the previous actor type set. When a
// serial message arrives for an instance of actor type C, it will first try 
// to construct the messages for actor type C, then for actor type B, and 
// finally for actor type A.
//
// The first issue could then be solved by the de-serialising actor having 
// a static structure mapping all the message types to their corresponding 
// sets of serialised message types. This would allow the messages to be 
// registered by type, and only once per actor type, and all instances of 
// serialising actors would share this database.
//
// However, the actors execute in separate threads, and therefore if two or more
// actors have received a serial message. They would then both need to access 
// the registered serial message types in this database. Hence there should be 
// a lock (mutex) serialising the access to the database. Furthermore, the lock
// must be kept until a message de-serialisation has been completed to success 
// or failure. This could severely hamper application performance. 
//
// Alternatively, each de-serialising actor instance could have its own map of 
// messages, This would duplicate the the database of serial messages supported
// by a particular actor for each instance. Access would be simpler in this 
// case since each actor could de-serialise the incoming messages in parallel 
// and independent of the activities of the other actors in the system. Hence,
// it is the classical trade-off between memory use and performance. 
//
// The current implementation emphasises performance, and the de-serialising 
// class defines its own database of messages supporting serialisation, and 
// this will therefore be unique to each instance of an actor supporting 
// serialisation. Hopefully, the number of messages supported by an actor is 
// not very large, and not too much memory will be wasted by this approach.


class DeserializingActor : virtual public Actor,
													 virtual public StandardFallbackHandler
{
private:
	
	// When a serial payload arrives it is given to a function that constructs 
	// the message, de-serialise the message, and if successful, it will forward
	// the message to the normal message handler.
	
	using MessageCreator = std::function< bool( 
															 const Theron::SerialMessage::Payload &,
														   const Address & ) >;
	
	// The messages supporting serialisation is kept in a standard map since it 
	// will be sequentially traversed when a serial payload comes in.
	
	std::map< std::type_index, MessageCreator > MessageTypes;

  // Since Theron allows multiple handlers to be registered for the same 
  // it is necessary to keep a count of handlers. This in order to be able 
  // to remove the message type once the last handler function is de-registered.
	// The normal way would have been to bundle this with the value type in the 
	// above map, but since the message types are parsed every time a message 
	// arrives while the handler count is only updated when a handler is 
	// registered, the counters are kept in a parallel map.

	std::map< std::type_index, unsigned int > HandlerCount;
	
	// Messages are registered with a message specific creator function that 
	// will forward the message to the right message handler provided that 
	// the message was correctly de-serialised. Note that there is no test to 
	// check that the message can be serialised since this test is best done 
	// prior to invoking this function.
	
	template< class MessageType >
	void RegisterMessageType( void )
	{
		static_assert( std::is_default_constructible< MessageType >::value,
							   "A serial message must have a default constructor" );

		// The function to construct this message is defined as a lambda passed 
		// and stored in the map for this type of messages.
		
		auto InsertResult =
		MessageTypes.emplace( typeid( MessageType ), 
								 [this]( const SerialMessage::Payload & Payload,
												 const Address & Sender )->bool
			 {
					MessageType BinaryMessage; 
		
					// There is a small issue with access. The Deserializing Actor is a 
					// friend of the serial message, but in general it cannot access 
					// protected members of derived message types. Hence the function to 
					// de-serialise the message must be called on a serial message, and 
					// then using the implementation of the derived class.
					
					SerialMessage * NewMessage( &BinaryMessage );
					
					if ( NewMessage->Deserialize( Payload ) )
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
	
	// Processing an incoming serial payload is then simply trying to construct 
	// the messages until one message type is successfully constructed. If no
	// messages are registered or if the end of the message type map is reached
	// with no successful construction, a runtime error is thrown.
	
  void SerialialMessageHandler (
		   const Theron::SerialMessage::Payload & Payload, 
		   const Theron::Address Sender )
	{
		if ( MessageTypes.empty() )
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " on line " << __LINE__ << " : "
									 << "Actor " << GetAddress().AsString() << " received"
									 << "serial message from " << Sender.AsString()
									 << " but no serial message types are registered";
									 
		  throw std::runtime_error( ErrorMessage.str() );
		}
		else
		{
			auto MessageCandidate = MessageTypes.begin();
			
			while ( ( MessageCandidate != MessageTypes.end() ) &&
							( MessageCandidate->second( Payload, Sender ) != true )	)
				++MessageCandidate;
			
			// If the payload did not correspond to any of the available messages,
			// an exception will be thrown as this situation should not occur. 
			
			if ( MessageCandidate == MessageTypes.end() )
		  {
				std::ostringstream ErrorMessage;
				
				ErrorMessage << __FILE__ << " on line " << __LINE__ << " : "
										 << "Actor " << GetAddress().AsString() << " received"
										 << "payload [" << Payload << "] from " << Sender.AsString()
										 << " which did not de-serialise to a known message";
										 
			  throw std::invalid_argument( ErrorMessage.str() );
			};
		}
	}

	// Given that there must be a message handler for all messages an actor can 
	// receive, the different message types can be captured when the message 
	// handler is registered. Consequently, the actor's register handler is 
	// overloaded with a version first registering the message type before 
	// forwarding the registration to the normal handler registration.
	//
	// The message type test is known at compile time and the optimiser should 
	// remove the if statement if the test fails leaving this as a simple 
	// instantiation of the actor's register handler. 
	//
	// Please note that the "if constexpr" is a C++17 feature, which may not yet
	// be supported by all compilers or at least produce a warning.
	
protected:
	
	template< class ActorType,  class MessageType >
  inline bool RegisterHandler( ActorType  * const TheActor, 
							 void ( ActorType::* TheHandler)(	const MessageType & TheMessage, 
																								const Address From ) )
	{
		if constexpr ( std::is_base_of<SerialMessage, MessageType>::value )
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
		bool ReturnValue = Actor::DeregisterHandler( HandlingActor, TheHandler );
		
		if ( ReturnValue )
			if ( --( HandlerCount[ typeid ( MessageType ) ] ) == 0 )
				 MessageTypes.erase( typeid( MessageType ) );
			
		return ReturnValue;
	}
	
  // The constructor is simply registering this handler for the framework to 
  // be ready for use directly.

public:
  
  DeserializingActor( const std::string name = std::string() )
  : Actor( name ),
    StandardFallbackHandler( GetAddress().AsString() )
  {
    RegisterHandler(this, &DeserializingActor::SerialialMessageHandler );		
  }

  // Backward compatibility constructor
  
  DeserializingActor( Framework & TheFramework, 
											const std::string name = std::string() )
	: DeserializingActor( name )
	{	}
    
  // And we need a virtual destructor to ensure that everything will be 
  // cleaned correctly.
  
  virtual ~DeserializingActor()
  { }	
};

} 			// End of name space Theron  
#endif 	// THERON_PRESENTATION_LAYER
