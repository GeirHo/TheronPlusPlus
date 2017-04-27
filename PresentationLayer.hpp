/*=============================================================================
  Presentation Layer
  
  Before a message can be sent over the network, it must be serialised. Even if 
  the remote side is binary compatible the message must be sent as a series of 
  bytes, although the serialisation in this case is trivial. In the general case
  the message must be translated into a byte string that will be well understood
  by the receiver and that allows the receiver to reassemble the data structure
  that has been sent.
  
  A message must be Serializable for it to be able to be sent across the 
  network. In terms of this framework it means that the message must inherit 
  the Serializable base class defined here, and implement the virtual function
  std::string Serialize() from that base class. It will be called by the 
  Presentation Layer to pack up the message before it is transmitted
    
  This class is a part of the revised and extended Theron mechanism for external
  communication. The Presentation Layer is a proxy for the remote agent, and 
  it receives messages to and from the network. Consider the following small 
  example: There are three actor of the same type A, B, and C exchanging data 
  in a complex message format (class). A and B are on the same EndPoint, 
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
  communication, the special class SerializedPayload is used for the message 
  format so that the actor can distinguish between strings that that contains 
  serialised binary structures and normal strings. The actual initialisation of 
  a binary message from a string should be done by the Deserialize method that 
  must be implemented for each message that should be transferable over the 
  network.
  
  To continue the above example: When a message arrives from C, the Presentation 
  Layer will receive a SerializedMessage with C as the sender and, say, B as the 
  receiver. The payload will be stored as a SerializedPayload and then forwarded
  to as if it comes from C, and B's handler for SerializedPayloads will receive 
  the message, convert it to the right binary format, and call B's handler for 
  the given message binary message type. 
  
  How to serialise a binary structure is up to the developer, and it is a topic 
  that has achieved quite some attention. The core problem is outlined by Oliver 
  Coudert [1], and there are several good C++ libraries that should be 
  considered to facilitate the serialisation: One of the first was 
  Boost::Serialization [2], although it has an issue with the serialisation of 
  std::shared_ptr [3]. Another approach that can work independent of the 
  programming languages used at either end is Google's Protocol Buffers [4]. Its
  messages can be larger than strictly necessary, and Yet Another Serialization
  (YAS) [5] aims to be faster than Boost::Serialization. Finally, Cereal [6] is
  a library that also supports binary encoding in addition to XML and JSON [7].
  There may be good libraries available for given message formats, like 
  JsonCpp [8], which generally receives good reviews for completeness and 
  performance, and use JSON messages among the actors. It is strongly 
  recommended to implement the serialising message handler using one of these 
  libraries and not implement the serialisation mechanism in a non-standard way.
  
  REFERENCES:
  
  [1] http://www.ocoudert.com/blog/2011/07/09/a-practical-guide-to-c-serialization/
  [2] http://www.boost.org/doc/libs/1_55_0/libs/serialization/doc/index.html
  [3] http://stackoverflow.com/questions/8115220/how-can-boostserialization-be-used-with-stdshared-ptr-from-c11
  [4] https://developers.google.com/protocol-buffers/
  [5] https://github.com/niXman/yas
  [6] http://uscilab.github.io/cereal/index.html
  [7] http://www.json.org/
  [8] https://github.com/open-source-parsers/jsoncpp
 
  REVISION: This file is NOT compatible with standard Theron - the new actor 
            implementation MUST be used.
 
  Author: Geir Horn, University of Oslo, 2015 - 2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef THERON_PRESENTATION_LAYER
#define THERON_PRESENTATION_LAYER

#include <string>
#include <sstream>
//#include <utility>
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <list>
#include <iterator>
#include <type_traits>
#include <stdexcept>
#include <mutex>

#include <Theron/Theron.h>
#include "StandardFallbackHandler.hpp"
#include "NetworkEndPoint.hpp"

// The Presentation Layer is defined to be a part of the Theron name space 

namespace Theron
{
 
/*****************************************************************************
  PRESENTATION LAYER
******************************************************************************/

// The Presentation Layer is itself an actor that will exchange messages with 
// other actors, and with the Session Layer server. 
  
class PresentationLayer : public virtual Actor,
													public virtual StandardFallbackHandler
{
public:
  
  // --------------------------------------------------------------------------
  // Serialised message format
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
  
  class SerialMessage
  {
  private:
    
    Address From, To;
    std::string Payload;
    
  public:
    
    SerialMessage( const Address & TheSender, const Address & TheReceiver, 
								   const std::string & ThePayload )
    : From( TheSender ), To( TheReceiver ), Payload( ThePayload )
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
    
    inline std::string GetPayload( void ) const
    {
      return Payload;
    }

  };
  
  // When a string is received from a remote actor, it is forwarded as a 
  // special class (identical to a string) so that the receiving object can 
  // de-serialise the message.
  
  class SerializedPayload : public std::string
  {
  public:
    
    SerializedPayload( const std::string & Payload ) 
    : std::string( Payload )
    { }
  };
  
  // A message that can be serialised must be derived from the Serializeable 
  // message base class, implicitly forcing the class to implement a function 
  // to serialise the message. Derived classes should provide constructors 
  // to build the binary message format from the serialised payload class above.
  // There is also a function to initialise the message from a serialised 
  // payload, and this returns a boolean to indicate true if the serialised 
  // payload correspond to the message structure and the initialisation was
  // successful.
  //
  // Note that the function to serialise a message will not change the original
  // message and it is therefore declared a constant so that it can be called 
  // on constant message objects.
  
  class Serializeable
  {
  public: 
    
    // First we indicate to the compiler that this message can be serialised
    
    typedef std::true_type IsSerializeable;
    
    // Then we can define the functions to deal with serialisation.
    
    virtual std::string Serialize( void ) const = 0;
    virtual bool        Deserialize( const SerializedPayload & Payload ) = 0;    
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
			// to an actor on this endpoint that should be of type Serial Message.
			// The real sending actor the real destination actor are encoded in the 
			// serial message.
			
			auto InboundMessage = 
					 std::dynamic_pointer_cast< Message< SerialMessage > >( TheMessage );
					 
		  // If the message conversion was successful, then this can be forwarded
		  // to the local destination actor as if it was sent from the remote 
		  // sender.
					 
		  if ( InboundMessage )
				Send( SerializedPayload( InboundMessage->TheMessage->GetPayload() ), 
							InboundMessage->TheMessage->GetSender(), 
							InboundMessage->TheMessage->GetReceiver() );
			else
				throw std::invalid_argument("Inbound message is not a Serial Message");
		}
		else
		{
			// The message should be outbound and it should be convertible to a 
			// serialised message
			
			auto OutboundMessage = 
					 std::dynamic_pointer_cast< Message< Serializeable > >( TheMessage );
					 
		  // A valid message will in this case be forwarded as a serial message 
		  // to the Session Layer server.
					 
		  if ( OutboundMessage )
				Send( SerialMessage( TheMessage->From, TheMessage->To, 
														 OutboundMessage->TheMessage->Serialize() ), 
							SessionServer );
			else
				throw std::invalid_argument("Outbound message is not Serializeable");
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
  // receive the other class' address as an argument to their constructors. The 
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
  
  PresentationLayer( NetworkEndPoint * TheHost,
								     std::string ServerName = "PresentationLayer"  ) 
  : Actor( ServerName ),
    StandardFallbackHandler( TheHost->GetFramework(), ServerName.data() ),
    SessionServer()
  {
		Actor::SetPresentationLayerServer( this );
  }
};

/*****************************************************************************
  DE-SERIALISING ACTOR
******************************************************************************/
// The presentation layer cannot know which serialised message types an actor 
// will be able to support, and therefore each actor using serialised messages
// must be able to construct these and invoke the appropriate message handlers
// after the binary message has been created.
//
// Unless it is easy to deduce from the payload which message type it is and 
// its completeness, which basically corresponds to the tasks of the de-
// serialise function, a brute force approach that will always work will be 
// to try to construct all supported messages that can be serialised, and 
// continue to the next supported message type if the construction fails. This
// general approach is implemented by the de-serialising actor, leaving the 
// implementation for the user to just provide the initialiser for the message
// types list. 
//
// The Actor is defined as a virtual base class to allow possible multiple 
// inheritance at the expense that each level of the heritage hierarchy has to 
// explicitly call the the Actor's constructor.

class DeserializingActor : virtual public Actor,
													 virtual public StandardFallbackHandler
{
protected:
  
  // The approach is to construct each message type based on the payload, and 
  // if the construction fails, then we will move on to the next message type.
  // This means that we will have to capture the exceptions thrown from the 
  // failing constructors since the constructor does not return a value, 
  // and then translate this to a positive or negative outcome. A dedicated 
  // function is used for this purpose. This function is generic and can be used
  // by any actor supporting serialised messages.
  //
  // This is declared as static because it is has no this pointer, and it is 
  // rather required that this is explicitly given, and it is explicitly used 
  // to forward the message to the calling actor.

  template< class MessageType >
  static bool ForwardMessage( const Theron::Actor * TheActor, 
		   const Theron::PresentationLayer::SerializedPayload & Payload,
		   const Theron::Address Sender  )
  {
    try
    {
      // We create the message based on the payload and if this does not lead
      // to an exception, we send the message to this actor (self) and report
      // a success.
      
      MessageType Message( Payload );
      TheActor->GetFramework().Send( Message, Sender, TheActor->GetAddress() );
      return true;
    }
    catch ( std::invalid_argument TheException )
    {
      // Creating the message from the given payload failed, and we should 
      // try with a different message format.
      
      return false;
    }
  }

  // The forwarding function must be instantiated for each supported message
  // that can be de-serialised, and these forwarding functions are kept in 
  // a list of function pointers. 
  
  typedef  std::list<  
	   std::function< bool( const Actor *,
			  const PresentationLayer::SerializedPayload &,
			  const Address ) > >  MessageForwardingList;
  
  
  // One should take care when inheriting from an actor that does already 
  // support message de-serialisation to ensure that all messages from all 
  // level of the inheritance three are defined in the message structure for 
  // the most derived object. Hence it should be possible to say something 
  // similar to the following concept
  // Derived::MessageTypes( <initialiser list> ).insert( 
  //  	Derived::MessageTypes.begin(),
  // 	Base::MessageList.begin(), Base::MessageList.end() )
  //
  // Note however that it might be necessary to construct a temporary list, 
  // and then initiate the real list from this, like indicated in 
  // http://stackoverflow.com/questions/780805/populate-a-static-member-container-in-c
  // 
  // The crux to achieve inheritable initialisation is to define this list as 
  // a dedicated class. Classes derived from the Deserializing Actor should 
  // derive from this class and define the supported message formats in its 
  // constructor. This will ensure that serialised messages supported by a 
  // base class will also be defined and supported by any derived type. 
  // Consider the following example:
  //  	Base : public DeserializingActor
  //    Derived : public Base
  //    Derived2 : public Derived
  // In this case the serialised messages supported by Base, Derived and 
  // Derived2 must all be supported by an instance of the Derived2 class. This
  // will happen when Derived2 declares a static variable which is derived 
  // from the message type structure since this will call the constructors of
  // its base defined in Derived, which will again call the constructor in Base
  // which will again call the constructor of the following class object.
  //
  // The constructor will typically contain a sequence of push_back calls for
  // the various message types directly supported by the class, e.g.
  // 
  // static Derived2::Messages : public Derived::Messages
  // { 
  //   public:
  // 	Derived2::Messages( void ) : Derived::Messages()
  //    {
  // 	  push_back( ForwardMessage< Derived2::Message1 > );
  //      push_back( ForwardMessage< Derived2::Message2 > );
  //    }
  // }
  //
  // Since actors by default are supposed to execute in parallel, there is a 
  // guarantee that each actor will have unique access to its own data. 
  // However, the message structure is supposed to be static and therefore 
  // shared among all instances of an actor type. The Theron framework may 
  // execute two actors at the same time in different threads, and therefore 
  // two actors can receive messages at the same time and access the message
  // type list. Hence there is a need to have a lock (mutex) for serialising 
  // access to the message construction functions. This mutex is a standard 
  // element of the static message type structure, and the de-serialising 
  // actor is given direct access to this element.

  class MessageTypeStructure : public MessageForwardingList
  {
  private:

    std::mutex MessageGuard;
    friend class DeserializingActor;
    
  public:
    
    MessageTypeStructure( void ) 
    : DeserializingActor::MessageForwardingList(), MessageGuard()
    { }
  };
  
  // The generalised handler for serialised messages must be able to step 
  // through the elements (messages) in this structure, and it is therefore 
  // a virtual function that must return a pointer to the static structure 
  // applicable for the particular actor type.
  
  virtual MessageTypeStructure * MessageTypes( void ) = 0;
  
  // There is a standard message handler for the serialised payload. If the 
  // general framework is used, it should not be necessary to overload this 
  // but if a derived actor wants to provide its own version, it is declared as 
  // virtual function.
  //
  // In particular, this function should learn over time which messages that 
  // are most frequently used, and move the forwarding functions for these 
  // messages forward in the structure. There are several known strategies 
  // known from the literature. The most famous one is to move the created 
  // message to the front of the list. However, this runs the risk that if the 
  // list is in an organised state, a simple access to one of the infrequently 
  // sent messages will will destroy the organisation and it will take many 
  // messages before the list is again organised. Alternatively, the received 
  // message can be moved k elements forward in the list. A compromise between
  // these two is to move the element to position k+1 if it it is in the end 
  // part of the list (move almost to front), and the transpose the element 
  // with the element in front if it is in the 2..k positions of the list. 
  // This heuristic is known as the POS(k) rule. Two different strategies 
  // are suggested in Oommen et al. (1990): "Deterministic optimal and 
  // expedient move-to-rear list organizing strategies", Theoretical Computer 
  // Science, Vol. 74, No. 2, pp. 183-197. Their first and optimal 
  // strategy implies keeping a counter for each element, and then move the 
  // accessed element to the rear of the list once it has been accessed T times.
  // Their expedient strategy implies moving the element to the rear of the 
  // list when it has been accessed T consecutive times. The implementation 
  // cost of the first rule would be similar to keeping a map sorted on the 
  // number of times each message has arrived - and this map will obviously be 
  // exact. Waiting for T consecutive accesses for an element may again be 
  // too slow. The transposition rule suggested by Ronald Rivest (1976): "On 
  // self-organizing sequential search heuristics", Communications of the ACM, 
  // Vol. 19, No. 2, pp. 63-67 is much more efficient. Here a successfully 
  // constructed message will be swapped with the element immediately in front
  // unless the element is already at the start of the list. This approach is 
  // taken in the implementation of the handler for serialised messages.
			
  virtual void SerializedMessageHandler (
		  const Theron::PresentationLayer::SerializedPayload & Payload, 
		  const Theron::Address Sender )
  {
    // Getting the pointer to the message type structure is a safe operation 
    // even before the lock is acquired since the elements of the structure 
    // is not accessed before the lock is effective, and the structure itself 
    // is static so its memory location will never be changed. The message 
    // lock object's destructor will release the lock once the function 
    // terminates.
    
    MessageTypeStructure * AvailableMessages = MessageTypes();
    std::unique_lock<std::mutex> MessageLock( AvailableMessages->MessageGuard );
    
    // The messages are parsed in order using iterators since the list 
    // optimisation would need to swap two elements, and this can conveniently 
    // be done by iterators. For each message type, we call its forwarding 
    // function with the payload and the sender, and if this function returns 
    // positively (true), then we apply Rivest' transposition rule.
    
    auto Message = AvailableMessages->begin();
    
    for ( ; Message != AvailableMessages->end(); ++Message )
      if ( (*Message)( this, Payload, Sender ) )
      { 
				// Optimise the list pushing this message type forward one position if 
				// this is not already the most popular message type.
				
				if ( Message != AvailableMessages->begin() )
				  std::iter_swap( Message, std::prev(Message) );
				
				// The Payload should correspond to one message type only, so there is 
				// no reason to continue testing more messages and we can safely return.
				
				return;
      }
      
    // If the search terminated with no message type found that corresponds to
    // the presented payload, there is a serious problem and this is indicated
    // by throwing a general run-time exception.
    
    if ( Message == AvailableMessages->end() )
    {
      std::ostringstream Error;
      
      Error << "Agent " << GetAddress().AsString() << " has received payload \""
            << Payload << "\" from agent " << Sender.AsString() 
				    << " not corresponding to any message type";
	    
      throw std::runtime_error( Error.str() );
    }
  }

  // ---------------------------------------------------------------------------
  // Constructor and destructor
  // ---------------------------------------------------------------------------

public:
  
  // The constructor is simply registering this handler for the framework to 
  // be ready for use directly.
  
  DeserializingActor( Framework & TheFramework, 
								      const std::string name = std::string() )
  : Actor( TheFramework, name.empty() ? nullptr : name.data() ),
    StandardFallbackHandler( TheFramework, GetAddress().AsString() )
  {
    RegisterHandler(this, &DeserializingActor::SerializedMessageHandler );		
  }
  
  // And we need a virtual destructor to ensure that everything will be 
  // cleaned correctly.
  
  virtual ~DeserializingActor()
  { }
};
} 	// End of namespace Theron  
#endif 	// THERON_PRESENTATION_LAYER
