/*==============================================================================
Active Message Queue interface

The Active Message Queue (AMQ) [1] is a server based messaging system where 
various clients can exchange messages via a server (message broker) using 
the following two models of communication.

1. Publish-subscribe: A "topic" is defined on the message broker and clients 
   in the system can subscribe to the topic and will receive the messages 
   other clients publish to this topic.
2. Queue-to-workers: A queue held by the message broker and the subscribers 
   receives the messages put to this queue in a round-robin way.
   
Implementing a distributed Theron++ actor system implies having actors on many 
different endpoints, and any actor should be able to send a message to any 
other actor. Essentially this calls for a peer-to-peer endpoint overlay network.
Furthermore, just like actors can be created and destroyed at run-time, 
endpoints of the system may come and go. Thus there is a need for mechanism 
that enables a new endpoint to join the communication graph of the other 
endpoints.

A publish-subscribe model requires some tweaking for a dynamic peer-to-peer 
system. A new endpoint can be directed to subscribe to one of the other peers' 
publish channel, but when the owner of that channel has no knowledge about the 
subscribers, it cannot subscribe back to establish a two-way communication with 
the new endpoint, and tell the new endpoint about other peers it may know about.
This can be solved in a peer-to-peer system by having a pre-defined topic for 
the endpoints. Ideally, this should be persisted so that new endpoints can just 
subscribe to the topic and thereby receive all past messages sent to this topic.
However, this may require a special configuration of the message broker, and 
it cannot be an assumption for the endpoint discovery. Instead, a very simple 
protocol is being used: Each joining endpoint simply subscribes to a pre-defined 
"TheronPlusPlus" topic on a given broker. This channel will be used for the 
actor discovery protocol described next.
   
There are three approaches possible for the actor-to-actor communication, all 
with their pros and cons:

I.   There is one common message channel where all endpoints post messages from 
     their actors. This is conceptually simple, but all messages from all goes 
     through this topic, and the messages arrives at all endpoint also those not 
     having an actor with the destination identifier.
II.  Each endpoint has an associated actor message topic and publishes messages 
     from its actors on this topic. Essentially this is the same as the first 
     option, but endpoints with no communication will not need to subscribe. 
     All subscribing endpoints gets all sent messages and receiver side filters
     must be applied to ignore messages for actors not on the current endpoint.
III. There is a destination actor discovery: The sending actor's endpoint sends 
     a discovery message on the pre-defined channel. The endpoint hosting the 
     destination actor sends a "hosting message" on the pre-defined 
     discovery topic. When this is received by the sending endpoint, it will 
     publish message on hold on the right endpoint-to-endpoint topic. This will 
     make the data traffic endpoint peer-to-peer, and avoid flooding for the 
     other endpoints, at the expense of a longer delay for the first message on 
     an actor-to-actor communication pair.
   
The one-to-one actor-to-actor communication is application defined and based 
on the actor names. Distributing the actor system across several endpoints 
can be done to increase performance or for architectural reasons, but it is 
still one actor system. Thus, there should be only one other endpoint 
responding to an actor discovery message. The outgoing message is then published
to in-box of the endpoint of the destination actor. Consequently, each endpoint 
subscribes to a AMQ topic having the endpoint name to receive messages for 
actors running on that endpoint.
   
The implementation here follows the concept of the Theron++ transparent 
communication over the following layers:

A. The network layer taking care of the AMQ specific parts. It initializes the 
   AMQ library, connects to the server, and subscribes to two topics: The 
   discovery channel "TheronPlusPlus" and the in-box for data messages with 
   the same name as the endpoint.
B. The session layer mapping actors to external endpoint (topics). When 
   receiving a discovery request it will check if the requested actor is local, 
	 if it is a response will be sent, otherwise the message will be discarded. 
	 When a discovery has been made, the external endpoint is cached as a topic 
	 for the external actor. As actors can be created and destroyed, and nobody
	 needs to inform anyone about such events, the cache will be considered 
	 valid only for a limited amount of time. By default one hour. If there has 
	 been no messages sent to a remote actor for that amount of time, the 
	 cached endpoint topic will be removed, and a new discovery must be done on 
	 the next message for this remote actor.

IMPORTANT: Since the transparent communication of Theron++ assumes that the 
messages sent and received are serialised, only text messages are supported in 
the current implementation. It should be the job of the presentation layer to 
implement a higher level protocol for serialising or de-serialising the arrived 
string.

When this AMQ interface is used, the application must be linked with the 
AMQ library and the SSL library is also necessary since the interface uses 
Open SSL for encrypted communication (if needed) according to Kevin Boone [4]:

-lactivemq-cpp -lssl

References:

[1] http://activemq.apache.org/
[2] http://activemq.apache.org/cms/index.html
[3] http://activemq.apache.org/cms/cms-api-overview.html
[4] http://kevinboone.net/cmstest.html

Author and Copyright: Geir Horn, 2017-2018
License: LGPL 3.0
==============================================================================*/

#ifndef THERON_ACTIVE_MESSAGE_QUEUE
#define THERON_ACTIVE_MESSAGE_QUEUE

// Standard headers

#include <memory>        // For smart pointers
#include <string>        // For standard strings
#include <map>           // For storing properties
#include <unordered_map> // For O(1) lookups
#include <set>           // For storing subscribing actors
#include <typeinfo>      // For knowing property types
#include <typeindex>     // For storing the property types
#include <sstream>       // For nice error messages
#include <stdexcept>     // For standard exceptions
#include <functional>    // Dynamic functions

// Headers for the Active Message Queue interface and the C++ Messaging System
// (CMS) [2]. Unfortunately, the C++ library for AMQ is written to be similar 
// to the Java interface and it therefore suffers from Java or C-style ways of 
// using the various classes and functions. It also passes message and object 
// pointers around without clarifying the ownership or using shared pointers.
// This interface is made more C++ like in this implementation, and pointers 
// to CMS objects are always protected by smart pointers if they are created 
// within the scope of this implementation.
//
// Note that the location of these files my be non-standard and the current 
// version is under 
// /user/include/activemq-cpp-3.9.4

#include <activemq/core/ActiveMQConnectionFactory.h>
#include <activemq/core/ActiveMQConnection.h>
#include <activemq/core/ActiveMQSession.h>
#include <activemq/cmsutil/CachedConsumer.h>
#include <activemq/cmsutil/CachedProducer.h>
#include <activemq/library/ActiveMQCPP.h>

#include <cms/Connection.h>
#include <cms/Session.h>
#include <cms/MessageListener.h>

// Headers for the Theron++ actor system

#include "Actor.hpp"
#include "StandardFallbackHandler.hpp"

#include "LinkMessage.hpp"
#include "SerialMessage.hpp"
#include "NetworkEndPoint.hpp"
#include "NetworkLayer.hpp"
#include "SessionLayer.hpp"

namespace Theron::ActiveMQ
{
	
// Setting the hard coded topic for all endpoints to listen to.
	
constexpr char DiscoveryTopic[] = "TheronPlusPlus";

/*==============================================================================
  
  External addresses

==============================================================================*/
//
// The global addresses of actors are supposed to be unique, and although all 
// actors on the same endpoint can be reached through the same in-box of the  
// end point hosting the actor, it prevents us from re-using the standard 
// functionality of the session layer mapping actor addresses to external 
// addresses. Thus the external address i defined analogous to the Jabber ID
// as "actor@endpoint".

class GlobalAddress
{
private:
	
	std::string ActorID, EndpointID;
	
public:
	
	// Access utility functions
	
	inline Address ActorAddress( void ) const
	{ return Address( ActorID ); }
	
	inline std::string ActorName( void ) const
	{ return ActorID; }
	
	inline std::string Endpoint( void ) const
	{ return EndpointID; }
	
	inline std::string AsString( void ) const
	{ return ActorID + "@" + EndpointID; }
	
	inline operator std::string ( void ) const
	{ return AsString(); }
	
	// Comparison functions based on the string representation
	
	inline bool operator == ( const GlobalAddress & Other ) const
	{ return (ActorID == Other.ActorID) && (EndpointID == Other.EndpointID); }
	
	inline bool operator < ( const GlobalAddress & Other ) const
	{ return AsString() < Other.AsString();	}
	
	inline bool operator <= ( const GlobalAddress & Other ) const
	{ return AsString() < Other.AsString(); }
	
	inline bool operator > ( const GlobalAddress & Other ) const
	{ return AsString() > Other.AsString(); }
	
	inline bool operator >= ( const GlobalAddress & Other ) const
	{ return AsString() >= Other.AsString(); }
	
	// Constructors. Note that the string constructor will throw if the 
	// string format does not confirm to the right format.
	
	inline
	GlobalAddress( const Address & TheActor, const std::string & TheEndpoint )
	: ActorID( TheActor.AsString() ), EndpointID( TheEndpoint )
	{}
	
	inline
	GlobalAddress( const std::string & TheActor, const std::string & TheEndpoint )
	: ActorID( TheActor ), EndpointID( TheEndpoint )
	{}
	
	inline GlobalAddress( const GlobalAddress & Other )
	: ActorID( Other.ActorID ), EndpointID( Other.EndpointID )
	{}

	GlobalAddress( void ) = delete;
	
	inline GlobalAddress( const std::string & StringAddress )
	{
		auto AtSign = StringAddress.find('@');
		
		if ( AtSign != std::string::npos )
	  {
			ActorID    = StringAddress.substr( 0, AtSign );
			EndpointID = StringAddress.substr( AtSign + 1 );
		}
		else
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
			             << "Global address: The given address string \""
									 << StringAddress << "\" does not have the right format: "
									 << "actor@endpoint";
									 
		  throw std::invalid_argument( ErrorMessage.str() );
		}
	}
	
};

// This address will be used in unordered maps that are based on the hash 
// functions for the string representation of the address. These hash functions
// must belong to the std name space for the standard maps, and to the boost 
// name space for the bi-map used by the session layer. It is therefore 
// necessary to close the name space for defining the hash functors.

} // end name space Theron Active MQ

// Specialisation for the standard map hash function

namespace std {
  template<>
  class hash< Theron::ActiveMQ::GlobalAddress >
  {
  public:
    
    size_t operator() ( const Theron::ActiveMQ::GlobalAddress & TheID ) const
    { return std::hash< std::string >()( TheID.AsString() ); }
  };
} // end name space std

// Specialisation for the boost bi-maps

namespace boost {
  template<>
  class hash< Theron::ActiveMQ::GlobalAddress >
  {
  public:
    
    size_t operator() ( const Theron::ActiveMQ::GlobalAddress & TheID ) const
    { return std::hash< std::string >()( TheID.AsString() ); }
  };	
} // end name space boost

// And then the Theron Active MQ name space can be opened again.

namespace Theron::ActiveMQ {
	
/*==============================================================================
  
  Messages

==============================================================================*/
//
// An outgoing AMQ message will have an AMQ topic or queue as destination 
// address and the sending actor identification as sender. An incoming message 
// AMQ message will have the remote actor as sender and the topic or queue 
// identification as recipient. Both of these are represented as simple strings
// for the standard link message.
//
// The message should have inherited the CMS message in protected mode, and 
// exposed only the acknowledge method for use by the receiving client if the 
// client acknowledges the message. However, owing to the non-C++ style of 
// the library, all message types derived from the CMS message inherits the 
// CMS message non-virtually. This means that there will be two copies of 
// the CMS message if it is inherited in this base class message. Even worse:
// The CMS message classes are pure virtual, so inheritance is not supported.
// One must use the creator methods, and access the CMS message classes through
// the pointer returned by the creator. The net effect of this is that the 
// message is tightly bound to the session creating it. This is no problem for 
// inbound messages created by the session layer actor when they arrive, but 
// it means that outbound messages cannot use the functions provided by the 
// standard CMS message classes. 
//
// Thus instead of storing a property by name and value directly in the message
// it must be cached generically and copied to the CMS message by the session 
// layer actor before transmitting the message.
//
// A further complication is to map the AMQ model onto actor-to-actor messages.
// A message in the actor system is supposed to have three elements: 
//
// 1. A string representing the sender actor's identification
// 2. A string representing the receiving actor's identification
// 3. A textual payload representing the serialised message.
//
// When using AMQ a subscription to a topic or queue is required prior to 
// be able to receive any messages. Two models are possible in this scenario:
// 
//  I. There may be one topic for each sending actor, and actors to receive 
//     messages from this actor needs to subscribe to its publishing channel
// II. There may be one topic per actor system endpoint (node) and all messages 
//     from actors on that endpoint goes out to that topic.
//
// The first approach is in practical terms impossible to make scalable because 
// all remote actors need to subscribe to the the topic of an actor when it 
// comes available, and unsubscribe when the actor closes. There will be an 
// overhead managing this protocol, and it frequently create new topics. 
//
// The second approach is therefore the only mechanism that can support the 
// dynamism of an actor system. Furthermore, it is possible to set filters 
// on the broker so that an endpoint only will receive messages sent to actors
// on that endpoint. 
//
// This approach requires that the addresses of sender actor and the receiver 
// is encoded as meta data of the message. In the AMQ language they are 
// therefore encoded as "properties" of the messages with the following 
// property names:
//	SendingActor    - textual actor identification
//  ReceivingActor  - textual actor identification
//
// It is also recognised that an actor may want to subscribe various other 
// topics providing application dependent information from non-actors, and 
// this is perfectly possible. In this case the sending actor identification 
// is set to the topic name. An actor should also be able to publish on a 
// given topic, and this is supported. Hence, the internal message must 
// carry The topic of a message, which is empty if the standard endpoint 
// actor-to-actor channel will be used. 

class Message
: public LinkMessage< GlobalAddress >
{
  // ---------------------------------------------------------------------------
  // Dealing with message properties
  // ---------------------------------------------------------------------------
  //
	// The CMS library distinguishes between a message payload and a message 
	// property. A property is fundamentally a message part referenced by 
	// a string label. However, properties of all basic types as well as strings
	// must be supported. C++ would allows a neat template solution to this 
	// problem, but since the actual storage in the CMS message will take place 
	// later, the properties must be cached.
	// 
	// The used method is to store the properties as a generic value class 
	// pointer in a map based on the property label, and call a virtual function
	// on the property value to store the value.
	
private:

  class PropertyValue
  {
	private:
		
		// The base class simply stores the type of the value in a way that it 
		// can be used to look up the type later.
		
		const std::type_index TypeID;
		
	public:
		
		virtual 
		void StoreProperty( const std::string & Label, 
											  cms::Message * TheMessage ) = 0;
		
		inline std::type_index GetType( void )
		{ return TypeID; }
		
		// The type ID is stored by the constructor
		
		PropertyValue( const std::type_index & ValueType )
		: TypeID( ValueType )
		{}
		
		// Which means that there cannot be a default constructor.
		
		PropertyValue( void ) = delete;
		
		// Since the class has virtual method it needs a virtual destructor
		
		virtual ~PropertyValue( void )
		{ }
	};
	
	// Then there is a template class for a given value type to be specialised 
	// below the class together with the functions to set and get the property 
	// values.
	
	template< class ValueType >
	class Value : public PropertyValue
	{
	public:
		
		const ValueType TheValue;
		
		// Since different functions has to be called for the different types, an 
		// implicit interface function must be specialised for each type. These 
		// specialisations cannot be done within the class, and it is done at the 
		// end of this header for readability reasons.
		
	  virtual 
	  void StoreProperty( const std::string & Label, 
		 								    cms::Message * TheMessage );

		// The constructor only needs the value since the type ID can be taken 
		// from the template parameter
		
		Value( const ValueType & GivenValue )
		: PropertyValue( typeid( ValueType ) ),
		  TheValue( GivenValue )
		{ }
		
		// The default constructor has no meaning for this class
		
		Value( void ) = delete;
		
		// The specific value type class also needs a virtual destructor
		
		virtual ~Value( void )
		{ }
	};
	
	// The property values are then stored as shared pointers in a map where the 
	// label is the key.
	
	std::map< std::string, std::shared_ptr< PropertyValue > > Properties;
	
	// With these definitions it is possible to define the public interface to 
	// set and get property values, the type and to check if the property label 
	// has been defined in order to verify the message end-to-end protocol. The 
	// function to set the property simply adds it to the map.
	
public:
	
	template< class ValueType >
	void SetProperty( const std::string & Label, const ValueType & GivenValue )
	{
		Properties.emplace( Label, 
												std::make_shared< Value< ValueType > >( GivenValue ) );
	}
	
	// Retrieving the value of a property by its label implies to first cast 
	// dynamically the base class pointer to the given value class and then 
	// return the desired value. If the label does not correspond to a property,
	// there will be an exception raised from the standard map, and if the 
	// pointer could not be converted to the right value class type a standard 
	// logic error exception will be thrown.
	
	template< class ValueType >
	ValueType GetProperty( const std::string & Label ) const
	{
		std::shared_ptr< Value< ValueType > > TheProperty
		= std::dynamic_pointer_cast< Value< ValueType > >( Properties.at( Label ) );
		
		if ( TheProperty )
			return TheProperty->TheValue;
		else
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "Message property " << Label << " was attempted to be "
									 << "retrieved as " << typeid( ValueType ).name();
									 
		  throw std::logic_error( ErrorMessage.str() );
		}
	}
	
	// There is also a generic way to get the type index of a property based on 
	// its label. Note that the at lookup function will throw if the label is 
	// not a legal property label. The properties defines the message protocol
	// between the sender and the receiver and therefore they should be known,
	// and if in doubt the label should be checked.
	
	inline std::type_index GetPropertyType( const std::string & Label ) const
	{
		return Properties.at( Label )->GetType();
	}
		
	// The function to check if a property exist simply searches the map for 
	// the given label
	
	inline bool PropertyExists( const std::string & Label ) const
	{
		return Properties.find( Label ) != Properties.end();
	}
	
	// There is a function to clear all the properties
	
	inline void ClearProperties( void )
	{
		Properties.clear();
	}
		
  // ---------------------------------------------------------------------------
  // Destination types, session types, and message type
  // ---------------------------------------------------------------------------
  //
	// There are basically two destinations for a message: it can be sent to a 
	// topic or it can be sent to a queue, and this decide how the message is 
	// handled. The topic is a publish-subscribe pattern, whereas the queue is 
	// a dealer pattern where messages are distributed round robin to the 
	// subscribers.

public:
	
	enum class Destination
	{
		Topic,
		Queue
	};

  // The session can also operate in three modes: It can be in transaction mode 
	// meaning that all messages are buffered and only sent when they are 
	// committed, or it can be in auto acknowledge mode where each message is 
	// sent individually and acknowledged by the receiver one by one. 
	
	enum class Session
	{
		AutoAcknowledge,    // Session layer acknowledge message by message
		TransactionStart,   // This is the first message of a transaction
		TransactionEnd      // This is the last message of a transaction
	};
	
	// Another shortcoming with the CMS or AMQ is that the messages does not 
	// carry a field indicating the type of the message. There are four types of
	// messages supported by the CMS, as copied from the CMS API:
	//
	// 1. A StreamMessage object's message body contains a stream of primitive 
	//    values in the C++ language. It is filled and read sequentially. Unlike 
	//    the BytesMessage type the values written to a StreamMessage retain 
	//    information on their type and rules for type conversion are enforced 
	//    when reading back the values from the Message Body.
	// 2. A MapMessage object's message body contains a set of name-value pairs, 
	//    where names are std::string objects, and values are C++ primitives. 
	//    The entries can be accessed sequentially or randomly by name. The 
	//    MapMessage makes no guarantee on the order of the elements within 
	//    the Message body.
	// 3. A TextMessage object's message body contains a std::string object.
	// 4. A BytesMessage object's message body contains a stream of uninterpreted 
	//    bytes. This message type is for literally encoding a body to match 
	//    an existing message format. In many cases, it is possible to use one 
	//    of the other message types, which are easier to use.
	//
	// All of these are defined as separate classes derived from this message 
	// class below. The enumeration is defined as an unsigned char (byte) for 
	// the applications where the used message protocol supports the addition of
	// this as a message property field.
	
	enum class Type : unsigned char
	{
		Unknown       = 0,
		StreamMessage = 1,
		MapMessage    = 2,
		TextMessage   = 3,
		BytesMessage  = 4
	};
	
	// These fields are accessible in public types fixed by the constructor of 
	// the message.
	
	const Destination DestinationMode;
	const Session     SessionType;
	const Type        MessageType;
		
	// ---------------------------------------------------------------------------
  // Utility functions
  // ---------------------------------------------------------------------------
  //
	// There is a helper function that can be used from derived classes to 
	// obtain the destination type from a received message. Note that the 
	// storage of the return value is needed to avoid warnings from the compiler
	
public:
	
	static inline Destination 
	GetDestination( const cms::Message * TheMessage )
	{
		Destination Result; 
		
		switch ( TheMessage->getCMSDestination()->getDestinationType() )
		{
			case cms::Destination::DestinationType::TOPIC:
			case cms::Destination::DestinationType::TEMPORARY_TOPIC:
				Result = Destination::Topic;
				break;
			case cms::Destination::DestinationType::QUEUE:
			case cms::Destination::DestinationType::TEMPORARY_QUEUE:
				Result = Destination::Queue;
				break;
		}
		
		return Result;
	}
	
	// Another utility function obtains the name of the queue or the topic by 
	// the same mechanism. Dynamic cast is needed to get the right type of 
	// pointer to access its name using the Real Time Type Information (RTTI)
	// system, and the cast will always succeed since the type of the destination
	// is tested first. Note also that temporary topics or queues do not have 
	// names available.

	static inline std::string 
	GetDestinationID( const cms::Message * TheMessage )
	{
		const cms::Destination * TheDestination = TheMessage->getCMSDestination();
		std::string Result;
		
		switch ( TheDestination->getDestinationType() )
		{
			case cms::Destination::DestinationType::TOPIC:
				Result = 
					dynamic_cast< const cms::Topic * >( TheDestination )->getTopicName();
				break;
			case cms::Destination::DestinationType::QUEUE:
				Result = 
					dynamic_cast< const cms::Queue *>( TheDestination )->getQueueName();
				break;
			default:
				Result = std::string();
		}
		
		return Result;
	}
	
	// A message should be converted to a CMS message of the right format. This 
	// must be defined for the individual message types, and its implementation 
	// is therefore left to the message type classes. It should be noted that 
	// the CMS message must be created elsewhere based on the session that 
	// will transmit the message, and the function will by default only set 
	// the properties. 
	//
	// In order to avoid double storage of the sender and the receiver actor 
	// identification (both as a property and as a part of the generic link 
	// message), they are taken directly from the link message fields.
	
	void StoreProperties( cms::Message * TheMessage ) const;
	
	// There is a function that takes the properties from the CMS message. 
	// This is fundamentally a big switch statement for the properties, and it 
	// is therefore defined in the source file and not in-place here. The 
	// destination mode is taken from the message, and the message type must be 
	// explicitly defined as this constructor is only assumed to be called from 
	// derived classes knowing their message type. The pointer can in this case
	// not be protected because the message object pointed to is typically 
	// created and maintained by the CMS library.
	
	void GetProperties( const cms::Message * TheMessage );
	
	// There is also an address mapper function. The idea is that the external 
	// actor address or identification could contain some encoding for a given 
	// transmission protocol. For instance, the endpoint hosting the actor could 
	// be a part of the actor address. In the AMQ case this function is just a 
	// an address encapsulation of the external actor's textual name.
	
	virtual 
  Address ActorAddress( const GlobalAddress & ExternalActor ) const override;
	
	// ---------------------------------------------------------------------------
  // Constructors and destructor
  // ---------------------------------------------------------------------------
  //
	// The simple constructor requires the global addresses of the two actors, 
	// the serialized payload, and optionally values for the mode and the type.
	
protected:
	
	inline Message( const GlobalAddress & SendingActor, 
									const GlobalAddress & ReceivingActor,
								  const Theron::SerialMessage::Payload & ThePayload,
								  const Type        MessageClass = Type::Unknown,
									const Destination Mode         = Destination::Topic, 
								  const Session     Transmission = Session::AutoAcknowledge )
	: LinkMessage< GlobalAddress >( SendingActor, ReceivingActor, ThePayload ),
	  Properties(), DestinationMode( Mode ), SessionType( Transmission ), 
	  MessageType( MessageClass )
	{
		SetProperty( "AMQDestination", ReceivingActor.Endpoint() );
	}
	
	// The copy constructor simply copies the properties, the destination mode, 
	// the session type, and the message type from the other message.
	
public:
	
	inline Message( const Message & Other )
	: LinkMessage< GlobalAddress >( Other.GetSender(), Other.GetRecipient(), 
																  Other.GetPayload() ),
	  Properties( Other.Properties.begin(), Other.Properties.end() ),
	  DestinationMode( Other.DestinationMode ), SessionType( Other.SessionType ),
	  MessageType( Other.MessageType )
	{}
		
	// The default constructor should not be used and it is therefore deleted
		
	Message( void ) = delete;
	
	// Since the message class has virtual methods it should also have a virtual
	// destructor to ensure proper deletion of derived classes even though there
	// is nothing to clean up for the message class.
	
	virtual ~Message( void )
	{ }
};


// -----------------------------------------------------------------------------
// Text message
// -----------------------------------------------------------------------------
//
// A text message should have been derived from the CMS text message, but as
// stated above this is not possible as the appropriate creator methods have 
// to be used. 
//
// Apart from this, the text message only holds an additional string payload, 
// and provides similar functions as the CMS text message to set and read the
// text payload. It should be noted, however, that this payload will be defined
// as a serial message payload to offer compatibility with the Theron++ 
// transparent communication layer.

class TextMessage : public Message
{	
public:
	
	// The standard constructor takes the topic or queue identification string
	// and possibly the payload, which can be given as an empty string if 
	// it will subsequently be initialised with the set text function or if the 
	// properties carries the necessary information.
	
	inline TextMessage( const GlobalAddress & SendingActor, 
										  const GlobalAddress & ReceivingActor,
										  const Theron::SerialMessage::Payload & ThePayload,
										  const Destination Mode         = Destination::Topic, 
										  const Session     Transmission = Session::AutoAcknowledge)
	: Message( SendingActor, ReceivingActor, ThePayload, Type::TextMessage, 
						 Mode, Transmission )
	{}
		
	// It can be copied...
	
 	inline TextMessage( const TextMessage & Other )
	: TextMessage( Other.GetSender(), Other.GetRecipient(), Other.GetPayload(),
								 Other.DestinationMode, Other.SessionType )
	{}
	
	// It can be constructed from a CMS text message. The rather involved 
	// definition is found in the source file. Note that this will throw an 
	// invalid argument if the pointer is null.
	
	TextMessage( const cms::TextMessage * TheTextMessage );
	
	// There is a utility that in-line converts any message to a text message 
	// before construction. It simply delegates to the previous constructor.
	
	inline TextMessage( const cms::Message * TheMessage )
	: TextMessage( dynamic_cast< const cms::TextMessage * >( TheMessage ) )
	{}
	
	// ...but it cannot be default constructed
	
	TextMessage( void ) = delete;
	
	// The destructor is virtual to allow proper deconstruction
	
	virtual ~TextMessage( void )
	{ }
};

/*==============================================================================

 Network layer

==============================================================================*/
//
// The link layer implements most of the AMQ specific issues ranging from the 
// initialisation of the protocol to the format conversion for the messages. 
// It receives outbound messages for a given topic from the session layer, 
// and forwards received messaged on a topic to the session layer which 
// maintains a mapping of actors subscribing to a particular topic.

class NetworkLayer 
: virtual public Actor,
  virtual public StandardFallbackHandler,
  virtual public Theron::NetworkLayer< TextMessage >
{
 	// ---------------------------------------------------------------------------
	// Connectivity
	// ---------------------------------------------------------------------------
	//
	// In order to connect to the AMQ server a connection and a session are 
	// necessary. 
	
private:
	
	activemq::core::ActiveMQConnection * AMQConnection;
	activemq::core::ActiveMQSession    * AMQSession;

  // ---------------------------------------------------------------------------
	// Handling AMQ Messages
	// ---------------------------------------------------------------------------
	//
	// The messages from other actors to actors on this endpoint is forwarded to
	// the session layer for further processing. Since only serialised messages 
	// are assumed, messages of other types of AMQ messages will lead to a
	// run time error exception.

private:
	
	void InboundMessage( const cms::Message * TheMessage );
	
	// Outbound messages arrives from the Session layer server with the topic set 
	// to the remote endpoints in-box. Thus the handler will just publish the 
	// message on that queue.

protected:
	
	virtual void OutboundMessage( const TextMessage & TheMessage, 
																const Address From ) override;
																
	// A command message arrives on the discovery topic when a remote 
	// endpoint wants to send a message to an actor whose location is unknown. 
	// Thus these requests should be ignored by endpoints not hosting the actor 
	// and a reply should include the endpoint name. 
	//
  // The currently devised AMQ protocol detects destination actors when another
  // actor tries to send a message to this actor. If the destination actor 
  // does not exist, i.e. there are no positive responses to the resolution 
  // request, it is undefined what to do and the session layer should cache 
	// the outbound message waiting for the actor to appear. 
	//
	// However, there is no obligation on an actor to register with the session 
	// layer when it starts up, and there is no guarantee that a particular 
	// remote actor ever will start up. The only possible protocol must therefore
	// be to retry the discovery request later. The session layer will do this 
	// for pending discovery requests every time it needs to do a new discovery.
	// Yet, it is possible to push the discovery response if the remote actor
	// registers with the session layer and the session layer pushes a discovery 
	// request for the local actor, see the handler function below.
																
private:
	
	enum class Command : short int
	{
		DiscoveryRequest  = 1,
		DiscoveryResponse = 2,
		ActorRemoved      = 3
	};

	// The handler for this receives the corresponding CMS message and responds 
	// if the actor is on this endpoint.
	
	void CommandHandler( const cms::Message * TheMessage );
	
	// A remote actor discovery is initiated by the session layer when a local 
	// actor wants to send a message to a remote actor. The session layer then 
	// sends a resolution request to the network layer, which must implement a
	// message handler for this request. The session layer may also send a 
	// resolve request for a local actor, and in this case a discovery response 
	// is generated to the other endpoints which may be waiting for this actor 
	// to become available; i.e. there may be unhanded discovery requests pending. 
	
protected:
	
	virtual void ResolveAddress( const ResolutionRequest & TheRequest, 
														   const Address TheSessionLayer ) override;
	
 	// The general view on communication is that an actor needs an external 
	// address before it can send a packet because responses will be delivered 
	// to this external address. Thus, when a local actor starts up it may 
  // register with the session layer which will in turn request the external 
  // address for this local actor using the resolve address method. In this 
  // case the external address may be created by the network layer encoding 
  // its own network location into the address. For push technologies, this 
  // may imply that the new actor address is pushed to the remote endpoints so
  // they are aware of the new actor. In this case it would be a need to inform 
  // remote endpoints when the actor disappears. For this reason a standard 
  // handler is provided to inform the network layer that a local actor is 
  // closing and that its external address can be invalidated.
  
  virtual void ActorRemoval( const RemoveActor & TheCommand, 
														 const Address TheSessionLayer ) override; 
  
	// ---------------------------------------------------------------------------
	// Receiving and sending AMQ messages
	// ---------------------------------------------------------------------------
	//
	// An AMQ topic or queue is a termed a destination, and it is possible both 
	// to subscribe to messages from a destination, or send messages to a 
	// destination. In this setting there is one common broadcast group subscribed
	// to by all endpoints and one in-box for each known endpoint. These should 
	// be fairly durable and probably be available for most of the execution time.
  // It would therefore be wasteful to create the destination object every time 
	// a message needs to be posted to a destination, and keep separate 
	// destination objects for subscribed destinations. It is therefore a map of
	// all destination objects. They are stored as unique pointers so that they 
	// will be deleted when the corresponding map object is deleted. There is a 
	// need for a quick look up, but not traversing the destinations in some 
	// sorted order and the unordered map is there an ideal structure for storing 
	// the destination objects.

private:
	
	std::unordered_map< std::string, std::unique_ptr< cms::Destination > >
	Destinations;
		
	// There is a small utility function to create the destinations depending on 
	// the type of the destination (topic or queue). 
	
	void CreateDestination( const std::string & TopicOrQueue, 
			 				            ActiveMQ::Message::Destination Type );
	
	// A producer is a passive object until it is used to send a message. 
	// Furthermore, the cached producer class supports sending to various 
	// destinations as an argument to the send function, and consequently each 
	// endpoint only needs one producer. However, this needs a producer resource,
	// and therefore both objects must be dynamically created.
	
	cms::MessageProducer *              ProducerResource;
	activemq::cmsutil::CachedProducer * Producer;
	
	// Listening to events from arriving messages is different since the message 
	// events will arrive asynchronously and the handling will be done by a 
	// dedicated class that can provide a dedicated function for handling the 
	// arrived message. The standard handler onMessage simply calls a handler 
	// specified at the construction to support different handlers for different
	// destinations.
	
	class MessageMonitor : public cms::MessageListener
	{
	private:
		
	  cms::MessageConsumer *              Consumer;
		activemq::cmsutil::CachedConsumer * Monitor;

		std::function< void( const cms::Message * TheMessage ) > Handler;
		
		// The pre-defined message handler just forwards the message pointer to 
		// the handler for processing.
		
		virtual void onMessage( const cms::Message* TheMessage ) override
		{
			Handler( TheMessage );
		}
		
		// The constructor initialises the listener and sets the handler based 
		// on the given handler function and start the monitor to listen to the 
		// topic. The handler is supposed to be a lambda function since it will 
		// invoke a member function on the network layer class.
		
	public:
		
		template< class LambdaFunction >
		MessageMonitor( cms::Session * TheSession, 
										const std::unique_ptr< cms::Destination > & Destination,
										const LambdaFunction & HandlerFunction )
		: Handler( HandlerFunction )
		{
		  Consumer = TheSession->createConsumer( Destination.get() );
			Monitor = new activemq::cmsutil::CachedConsumer( Consumer );
			Monitor->setMessageListener( this );
			Monitor->start();
		}
		
		// There is no default constructor that can be accidentally used.
		
		MessageMonitor( void ) = delete;
		
		// The destructor closes the monitor and deletes the destination object.
		
		~MessageMonitor( void )
		{
			Monitor->stop();
			delete Monitor;
			delete Consumer;
		}
	};
	
  // The message monitors are kept in the same sort of map from the topic or 
	// queue identifier to a unique pointer for the monitor class. However, these
	// pointers will be created when a new subscription is made and deleted when 
	// the subscription is cancelled. Thus these message handers maintain the 
	// active monitors and stops the one for which the subscription is cancelled.
	
	std::unordered_map< std::string, std::unique_ptr< MessageMonitor > >
	Subscriptions;

	// ---------------------------------------------------------------------------
	// Actor managed subscriptions
	// ---------------------------------------------------------------------------
	//
  // It is easy to imagine that the AMQ server (broker) could be used for other 
	// purposes for some applications. One example is when there are other 
	// applications running providing sensor values and measurements to the actors
	// of the distributed actor system. Then the receiving actors need to manage 
	// the subscriptions themselves. This is done by sending a subscribe request 
	// to the session layer, and the session layer will then send a subscribe 
	// request to the network layer. This is important because there need not be 
	// a one-to-one mapping between subscriptions and actors. There could be many 
	// actors subscribing to the same topic, and one actor may subscribe to many 
	// topics. 
	//
	// Once a topic has been subscribed to, it will be a viable destination and 
	// it is also possible to send messages to the same topic. Incoming messages 
	// will be managed exactly as messages on the in-box and just forwarded to 
	// the session layer for distribution.
	
public:
	
	class CreateSubscription
	{
	public:
		
		const std::string                    TopicOrQueue;
		const ActiveMQ::Message::Destination ChannelType;
		
		CreateSubscription( const std::string NameOfChannel, 
												const ActiveMQ::Message::Destination Type )
		: TopicOrQueue( NameOfChannel ), ChannelType( Type )
		{}
		
		CreateSubscription( const CreateSubscription & Other )
		: TopicOrQueue( Other.TopicOrQueue ), ChannelType( Other.ChannelType )
		{}
		
		CreateSubscription( void ) = delete;
	};
	
	// There is a handler for this message that creates the destination and sets
	// up the subscription.
	
private:
	
	void NewSubscription( const CreateSubscription & Request, 
												const Address From );
	
	// Cancelling a subscription follows the same pattern where the session layer
	// sends a cancel subscription message. In this case only the channel name 
	// is needed. 
	
public:
	
	class CancelSubscription
	{
	public:
		
		const std::string TopicOrQueue;
		
		CancelSubscription( const std::string & NameOfChannel )
		: TopicOrQueue( NameOfChannel )
		{}
		
		CancelSubscription( const CancelSubscription & Other )
		: TopicOrQueue( Other.TopicOrQueue )
		{}
		
		CancelSubscription( void ) = delete;
	};
	
	// Given that the actor system is asynchronous there may be incoming messages
	// on this queue over the time epoch from the moment the session layer sends
	// the cancellation request until it has been implemented by the network 
	// layer. The handler for the cancel subscription will therefore return the 
	// cancel subscription message back to the session layer (sender) to notify 
	// that this topic is now completely removed.
	
private:
	
	void RemoveSubscription( const CancelSubscription & Request, 
													 const Address From );
		
	// ---------------------------------------------------------------------------
	// Constructor and destructor
	// ---------------------------------------------------------------------------
	//
	// The constructor takes the global identifier of this network endpoint as 
	// its name and the IP of the server to connect to and optionally the port 
	// used to reach the AMQ message broker. If no port is given, the standard 
	// 61616. 
	
public:
	
	NetworkLayer( const std::string & EndpointName, 
								const std::string & AMQServerIP, 
							  const std::string & Port = "61616" );

	// The default constructor is deleted
	
	NetworkLayer( void ) = delete;
	
	// The destructor closes the connection and stops the AMQ library
	
	virtual ~NetworkLayer( void );
};

/*==============================================================================

 Session layer

==============================================================================*/


/*==============================================================================

 Message: specialisation of functions storing the properties

==============================================================================*/
//
// Boolean specialisation

template<>
void Message::Value< bool >::StoreProperty( 
	const std::string & Label, cms::Message * TheMessage )
{
	TheMessage->setBooleanProperty( Label, TheValue );
}

// Byte specialisation

template<>
void Message::Value< unsigned char >::StoreProperty( 
	const std::string & Label, cms::Message * TheMessage )
{
	TheMessage->setByteProperty( Label, TheValue );
}

// Double specialisation

template<>
void Message::Value< double >::StoreProperty( 
	const std::string & Label, cms::Message * TheMessage )
{
	TheMessage->setDoubleProperty( Label, TheValue );
}

// Float specialisation

template<>
void Message::Value< float >::StoreProperty( 
	const std::string & Label, cms::Message * TheMessage )
{
	TheMessage->setFloatProperty( Label, TheValue );
}

// Integer specialisation

template<>
void Message::Value< int >::StoreProperty( 
	const std::string & Label, cms::Message * TheMessage )
{
	TheMessage->setIntProperty( Label, TheValue );
}

// Long specialisation

template<>
void Message::Value< long long int >::StoreProperty( 
	const std::string & Label, cms::Message * TheMessage )
{
	TheMessage->setLongProperty( Label, TheValue );
}

// Short specialisation

template<>
void Message::Value< short int >::StoreProperty( 
	const std::string & Label, cms::Message * TheMessage )
{
	TheMessage->setShortProperty( Label, TheValue );
}

// String specialisation

template<>
void Message::Value< std::string >::StoreProperty( 
	const std::string & Label, cms::Message * TheMessage )
{
	TheMessage->setStringProperty( Label, TheValue );
}

}      // End of name space Active MQ
#endif // THERON_ACTIVE_MESSAGE_QUEUE
