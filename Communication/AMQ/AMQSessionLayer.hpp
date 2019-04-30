/*==============================================================================
Active Message Queue session layer

The Active Message Queue (AMQ) [1] is a server based messaging system where 
various clients can exchange messages via a server (message broker) using 
the following two models of communication.

1. Publish-subscribe: A "topic" is defined on the message broker and clients 
   in the system can subscribe to the topic and will receive the messages 
   other clients publish to this topic.
2. Queue-to-workers: A queue held by the message broker and the subscribers 
   receives the messages put to this queue in a round-robin way.
   
The actor implemented here serves as a transparent interface to the AMQ broker
accepting messages from other actors to be forwarded to either a queue or a 
topic. Subscriptions are handled by forwarding the received message to the 
subscribing local actors. 

This implies that the the actor may also serve as a local broker if no 
connection can be established to the remote broker, and this will be 
transparent for the local actors whether they publish over the network or just 
locally.

When this actor is used, the application must be linked with the AMQ library 
and the SSL library is also necessary since the interface uses Open SSL for 
encrypted communication (if needed) according to Kevin Boone [4]:

-lactivemq-cpp -lssl

References:

[1] http://activemq.apache.org/
[2] http://activemq.apache.org/cms/index.html
[3] http://activemq.apache.org/cms/cms-api-overview.html
[4] http://kevinboone.net/cmstest.html

Author and Copyright: Geir Horn, 2017-2018
License: LGPL 3.0
==============================================================================*/

#ifndef THERON_ACTIVEMQ_CLIENT
#define THERON_ACTIVEMQ_CLIENT

// Standard headers

#include <memory>      // For smart pointers
#include <string>      // For standard strings
#include <map>         // For storing properties
#include <set>         // For storing subscribing actors
#include <typeinfo>    // For knowing property types
#include <typeindex>   // For storing the property types
#include <sstream>     // For nice error messages
#include <stdexcept>   // For standard exceptions

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

#include <activemq/library/ActiveMQCPP.h>
#include <activemq/core/ActiveMQConnectionFactory.h>
#include <cms/Connection.h>

// Headers for the Theron++ actor system

#include "Actor.hpp"
#include "SerialMessage.hpp"

namespace Theron::ActiveMQ 
{
/*==============================================================================

 Messages

==============================================================================*/
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

class Message
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
	public:
		
		virtual void StoreProperty( const std::string & Label, 
															  cms::Message * TheMessage ) = 0;
		
		virtual std::type_index GetType( void ) = 0;
		
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
		
		const ValueType       TheValue;
		const std::type_index TypeID;
		
		// Since different functions has to be called for the different types, an 
		// implicit interface function must be specialised for each type. These 
		// specialisations cannot be done within the class, and it is done at the 
		// end of this header for readability reasons.
		
	  virtual void StoreProperty( const std::string & Label, 
															  cms::Message * TheMessage ) override;

	  // The type index can be readily returned 
									 
		virtual std::type_index GetType( void ) override
		{ return TypeID; }
		
		// The constructor only needs the value since the type ID can be taken 
		// from the template parameter
		
		Value( ValueType & GivenValue )
		: TheValue( GivenValue ), TypeID( typeid( ValueType ) )
		{ }
		
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
	ValueType GetProperty( const std::string & Label )
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
	
	inline std::type_index GetPropertyType( const std::string & Label )
	{
		return Properties.at( Label )->GetType();
	}
		
	// The function to check if a property exist simply searches the map for 
	// the given label
	
	inline bool PropertyExists( const std::string & Label )
	{
		return Properties.find( Label ) != Properties.end();
	}
	
	// There is a function to clear all the properties
	
	inline void ClearProperties( void )
	{
		Properties.clear();
	}
		
	// There are also virtual functions to clear the body of a message and to 
	// acknowledge the message. These cannot be defined in the base class and 
	// it is therefore necessary to define them in the classes defining the 
	// message types.
	
	virtual void ClearBody( void ) = 0;
	virtual void Acknowledge( void ) const = 0;
	
  // ---------------------------------------------------------------------------
  // Destination types, session types, and message type
  // ---------------------------------------------------------------------------
  //
	// There are basically two destinations for a message: it can be sent to a 
	// topic or it can be sent to a queue, and this decide how the message is 
	// handled. The topic is a publish-subscribe pattern, whereas the queue is 
	// a dealer pattern where messages are distributed round robin to the 
	// subscribers.

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
	
	// There is also a destination ID which is a simple string identifying to 
	// which topic or queue the message belongs.
	
	const std::string DestinationID;
	
	// The CMS message pointer type is defined so that the compiler knows how 
	// to convert the pointer
	
	using CMSPointer = cms::Message * ;

	// ---------------------------------------------------------------------------
  // Utility functions
  // ---------------------------------------------------------------------------
  //
	// There is a helper function that can be used from derived classes to 
	// obtain the destination type from a received message
	
public:
	
	static inline Destination GetDestination( const cms::Message * TheMessage )
	{
		switch ( TheMessage->getCMSDestination()->getDestinationType() )
		{
			case cms::Destination::DestinationType::TOPIC:
			case cms::Destination::DestinationType::TEMPORARY_TOPIC:
				return Destination::Topic;
				break;
			case cms::Destination::DestinationType::QUEUE:
			case cms::Destination::DestinationType::TEMPORARY_QUEUE:
				return Destination::Queue;
				break;
		}
	}
	
	// Another utility function obtains the name of the queue or the topic by 
	// the same mechanism. Dynamic cast is needed to get the right type of 
	// pointer to access its name using the Real Time Type Information (RTTI)
	// system, and the cast will always succeed since the type of the destination
	// is tested first. Note also that temporary topics or queues do not have 
	// names available.

	static inline std::string GetDestinationID( const cms::Message * TheMessage )
	{
		const cms::Destination * TheDestination = TheMessage->getCMSDestination();
		
		switch ( TheDestination->getDestinationType() )
		{
			case cms::Destination::DestinationType::TOPIC:
				return 
					dynamic_cast< const cms::Topic * >( TheDestination )->getTopicName();
				break;
			case cms::Destination::DestinationType::QUEUE:
				return 
					dynamic_cast< const cms::Queue *>( TheDestination )->getQueueName();
				break;
			default:
				return std::string();
				break;
		}
	}
	
	// A message should be converted to a CMS message of the right format. This 
	// must be defined for the individual message types, and its implementation 
	// is therefore left to the message type classes. It should be noted that 
	// the CMS message must be created elsewhere based on the session that 
	// will transmit the message, and the function will by default only set 
	// the properties. 
	
	virtual void ConvertToCMS( cms::Message * TheMessage )
	{
		for ( auto & TheProperty : Properties )
			TheProperty.second->StoreProperty( TheProperty.first, TheMessage );
	}
	
	// ---------------------------------------------------------------------------
  // Constructors and destructor
  // ---------------------------------------------------------------------------
  //
	// The simple constructor requires this ID and optionally values for the mode 
	// and the type.
	
	inline Message( const std::string & TopicOrQueue,
									const Type        MessageClass = Type::Unknown,
									const Destination Mode = Destination::Topic, 
								  const Session     Transmission = Session::AutoAcknowledge )
	: Properties(), DestinationMode( Mode ), SessionType( Transmission ), 
	  MessageType( MessageClass ), DestinationID( TopicOrQueue )
	{ }
	
	// The copy constructor simply copies the properties, the destination mode, 
	// the session type, and the message type from the other message.
	
	inline Message( const Message & Other )
	: Properties( Other.Properties.begin(), Other.Properties.end() ),
	  DestinationMode( Other.DestinationMode ), SessionType( Other.SessionType ),
	  MessageType( Other.MessageType ), DestinationID( Other.DestinationID )
	{ }
	
	// There is a constructor that constructs the message from a CMS message. 
	// This is fundamentally a big switch statement for the properties, and it 
	// is therefore defined in the source file and not in-place here. The 
	// destination mode is taken from the message, and the message type must be 
	// explicitly defined as this constructor is only assumed to be called from 
	// derived classes knowing their message type. The pointer can in this case
	// not be protected because the message object pointed to is typically 
	// created and maintained by the CMS library.
	
protected:
	
	Message( const cms::Message * TheMessage,
					 const Type MessageClass,
					 const Session Transmission = Session::AutoAcknowledge );
	
	// The default constructor should not be used and it is therefore deleted
	
public:
	
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

class TextMessage : virtual public Message
{
private:
	
	SerialMessage::Payload ThePayload;
	
public:
	
	// The CMS pointer type is defined for text messages
	
	using CMSPointer = cms::TextMessage *;
	
	// There are functions to get and set the text payload of the message
	
	inline SerialMessage::Payload GetText( void ) const
	{ return ThePayload; }
	
	inline void SetText( const std::string & PayloadText )
	{ ThePayload = PayloadText;	}
	
	// The mandatory virtual functions can readily be defined
	
	virtual void ClearBody( void ) override
	{	ThePayload.erase();	}
	
	// The standard constructor takes the topic or queue identification string
	// and possibly the payload, which can be given as an empty string if 
	// it will subsequently be initialised with the set text function or if the 
	// properties carries the necessary information.
	
	inline TextMessage( const std::string & TopicOrQueue, 
							        const std::string & PayloadText = std::string(), 
										  const Destination Mode = Destination::Topic, 
										  const Session     Transmission = Session::AutoAcknowledge)
	: Message( TopicOrQueue, Type::TextMessage, Mode, Transmission ),
	  ThePayload( PayloadText )
	{	}
	
	// The text message can also be constructed from a text message pointer. 
	// This is the typical inbound message constructor, and therefore the message
	// pointer is created and owned by the CMS.
	
	inline TextMessage( const cms::TextMessage * TheMessage, 
										  const Session Transmission = Session::AutoAcknowledge )
	: Message( TheMessage, Type::TextMessage, Transmission ),
	  ThePayload( TheMessage->getText() )
	{ }
	
	// ...but it cannot be default constructed
	
	TextMessage( void ) = delete;
	
	// The destructor is virtual to allow proper deconstruction
	
	virtual ~TextMessage( void )
	{ }
};

	
/*==============================================================================

 Session Layer

==============================================================================*/
//
// The actor class encapsulating the Active MQ interface is performing tasks 
// close to the role of the session layer of the OSI stack mapping external 
// and internal communication layers and maintaining sessions.
	
class SessionLayer
: virtual public Actor
{
private:
	
  // ---------------------------------------------------------------------------
  // AMQ library initialisation
  // ---------------------------------------------------------------------------
  //
	// One example of the Java style shortcoming is the need to call a function 
	// to initialise the library prior to the first use of any functions. This 
	// creates a problem since the constructor of this session layer will 
	// need to ensure that this initialisation function is called prior to any 
	// of the other AMQ methods. The solution is to create a small initialiser 
	// class whose constructor will be executed first when the session layer 
	// starts up, and therefore this constructor may call the required AMQ 
	// initialiser function.
	//
	// Similarly, it also ensures that the library shut down is performed when 
	// the session layer closes.
	
	class AMQInitialiser
	{
	private:
		
		static bool Uninitialised;
		
	public:
		
		AMQInitialiser( void )
		{
			if ( Uninitialised )
		  {
				activemq::library::ActiveMQCPP::initializeLibrary();
				Uninitialised = false;
			}
			  
		}
		
		~AMQInitialiser( void )
		{
			if ( Uninitialised == false )
		  {
				activemq::library::ActiveMQCPP::shutdownLibrary();
				Uninitialised = true;
			}
				
		}
		
	} InitialiseLibrary;
	
  // ---------------------------------------------------------------------------
  // Connection management
  // ---------------------------------------------------------------------------
  //
	// In order to set up a session a Connection Factory and a 
	// Connection must be created. These are stored as static variables in the 
	// case that more session layers are created for the Active MQ, which is 
	// really unnecessary, but should not break the code. They are stored as 
	// shared pointers to ensure that they are deleted when the application 
	// terminates. They cannot be static objects because the CMS and the AMQ 
	// library must be initialised before these can be constructed.
	
	static std::shared_ptr< cms::ConnectionFactory > Factory;
	static std::shared_ptr< cms::Connection >        Connection;
	
  // ---------------------------------------------------------------------------
  // Dispatching messages
  // ---------------------------------------------------------------------------
  //
	// The default intention of the session layer is to provide a gateway to 
	// remote parts of the actor system, and therefore it assumes that the 
	// sender's address of a message should equal the destination identification
	// of the transaction. However, the method is virtual to this default 
	// behaviour to be changed, along with the message types supported. The 
	// latter is changed by overloading the onMessage method on the Transaction 
	// object.

protected:
	
	virtual Address GetSenderAddress( const cms::Message * GenericMessage )
	{
		return 
		Address( Theron::ActiveMQ::Message::GetDestinationID( GenericMessage ) );
	}
	
  // ---------------------------------------------------------------------------
  // Transactions and sessions
  // ---------------------------------------------------------------------------
  //
	// The session layer maintains the sessions on the connection. The sessions 
	// are fundamentally distinguished by the way messages are sent and 
	// acknowledged. There are two types of sessions supported by this actor:
	// 
	// 1. Automatic acknowledgement of each message as soon as the message 
	//    handler returns from a call. This is the default mode. 
	// 2. Transacted transmission where a set of messages can be stored and 
	//    then sent as one block. It is not clear how this works for incoming 
	//    messages.
  //
	// There are two other types that could be of interest: The first is the 
	// client acknowledge. An acknowledgement is done by the receiving client 
	// by calling  a method on the message. However, what is a client here? 
	// Seen from CMS it is this session layer actor as it is explicitly stated 
	// that the message whose pointer is passed to the message handler only 
	// remains allocated until the message handler terminates. However, seen 
	// from the actor system, the client should be the receiving actor. This
	// would require that actor to send an acknowledgement back to the session 
	// layer, which would then acknowledge the message it has put on hold. Actor
	// client acknowledgement therefore implies a synchronisation between the 
	// session layer and the client actor, and such synchronisation should in 
	// general be avoided in an actor system. It should also block the session 
	// layer's message handler from terminating before the acknowledgement has 
	// been processed. In practice this means that the session layer should run 
	// in a separate thread, i.e. a receiver, that can block and wait for the 
	// acknowledgement from the client actor. If client actor acknowledgement 
	// is needed by the application it should rather be implemented as an 
	// end-to-end acknowledgement protocol between the remote sending actor 
	// and the local client actor.
	//
	// The second interesting session type is "individual acknowledgement". A
	// subscription is supposed to be "durable". This implies that even if 
	// something disconnects the physical node hosting the session layer server 
	// from the remote message broker, the broker will keep messages and send 
	// them once the connection is restored. With automatic acknowledgement the 
	// CMS is free to send the acknowledgement to the broker once the last 
	// buffered message has been received to minimise traffic between the broker 
	// and the node hosting this session layer server. With individual 
	// acknowledgement the broker will receive an acknowledgement for each of 
	// the messages as it is consumed by the session layer server. This will 
	// increase network traffic, but will allow the broker to delete a (large) 
	// message that have been seen by all subscribers as soon as the last 
	// subscriber acknowledge the message.
	//
	// Lazy initialisation is used, so the CMS sessions will only be created 
	// once they are needed, i.e. when the first actor subscribes or send a 
	// message requiring a particular session type.
	
public:
	
	enum class SessionType : unsigned short int
	{
		AutomaticAck = cms::Session::AcknowledgeMode::AUTO_ACKNOWLEDGE,
		Transacted   = cms::Session::AcknowledgeMode::SESSION_TRANSACTED
	};
		
	// A transaction is understood to be either a topic or a queue. It has a 
  // name (or ID), and one producer and one consumer. A list of actors 
	// subscribing to the transaction is maintained, and incoming messages
  // are dispatched to all subscribers. An actor needs to explicitly subscribe.
  // This means that even if an actor publishes to a transaction, it will not 
	// receive responses from this transaction unless it has subscribed to it. 
	// The subscribers defines the duration of the transaction, and it will be 
	// closed if there are no subscribers. Thus, if an actor publish to a topic,
	// this topic will not be remembered by the session layer unless the actor 
	// (or any other actor) has subscribed to the topic before publishing.
	//
	// The transaction implements the onMessage method of the message listener 
	// and this method will be called when the consumer receives a message. It 
	// will convert the inbound message to one of the message classes above and 
	// use the session layer's send function to forward these to all local 
	// subscribing actors. Note that calling the Send method on the session layer
	// will not violate the actor model since the transaction class is a part 
	// of the session layer and is just a way to organise the various data 
	// fields of the session layer. It could have been directly implemented as
	// methods and data structures in the session layer, but it would make the 
	// code more involved and more difficult to understand.
	//
	// Implementation note: The consumer and the producer is created by a 
	// Destination object, which again is created by a session object. It is not 
	// clear from the documentation whether this destination object should be 
	// kept for the lifetime of the producers and consumers created for it, or
	// if the consumers and producers store the necessary information so that 
	// the destination object can be deleted once these objects are created. 
	// The latter approach is taken here, and the destination object is therefore
	// a temporary object created and destroyed by the transaction's constructor.
	
private:
	
	class Transaction : public cms::MessageListener
	{
	private:
				
		std::shared_ptr< cms::MessageProducer > Producer; // Outbound messages
		std::shared_ptr< cms::MessageConsumer > Consumer; // Inbound messages
		
		// The subscribing actors are stored in a set to ensure that each actor 
		// only subscribes once.
		
		std::set< Address > Subscribers;
		
		// If the transaction should be able to forward inbound messages to the 
		// subscribing actors, it needs a Actor::Send function provided by the 
		// session layer. Hence, it needs a pointer to this session layer
		
		SessionLayer * ThisSessionLayer;
		
	// There is no CMS enumeration in the message to indicate the type of a 
	// message, and an arriving message is returned as a base class message 
	// pointer. It therefore relies on the Real Time Type Information (RTTI)
	// to cast the pointer to the right type that can be used to construct 
	// the right message type class (as defined above). The pattern will be 
	// the same for all message types, and a template is defined to handle 
	// this. It has the standard format of an alias send function, with the 
	// message first, followed by the sender actor address and a set of receiver 
	// addresses.
	
	template< class MessageType >
	bool DispatchMessage( const cms::Message * GenericMessage )
	{
		typename MessageType::CMSPointer TheMessage 
			= dynamic_cast< typename MessageType::CMSPointer >( GenericMessage );
			
		if ( TheMessage != nullptr )
	  {
			MessageType MessageObject( TheMessage );
			Address     TheSender( 
										ThisSessionLayer->GetSenderAddress( GenericMessage ) ) ;
			
			for ( const Address & TheSubscriber : Subscribers )
				ThisSessionLayer->Send( MessageObject, TheSender, TheSubscriber );
			
			return true;
		}
	  else return false;
	}

	public:
		
		// Since this class is a message listener, it has an onMessage method that
		// will be called by the consumer when it receives a message. Since it is 
		// a virtual method, it will be defined in the source file, but basically
		// it just calls the dispatch message for each type of message that can 
		// arrive.
		
		virtual void onMessage( const cms::Message * TheMessage ) override;
		
		// The simplest method gets a message and sends it on the channel. If the 
		// session layer is not connected, the message is just sent to the local 
		// subscribers of this topic or queue by calling the standard message 
		// listener's onMessage method. Note that the message pointer here is 
		// protected since it is outbound and created by the session layer when 
		// it converts an outbound message to a CMS message.
		
		inline void Send( const std::shared_ptr< cms::Message > & TheMessage )
		{
			if ( Producer )
				Producer->send( TheMessage.get() );
			else
				onMessage( TheMessage.get() );
		}
		
		// There are simple methods to add or remove subscribers from the set
		
		inline void AddSubscription( const Address & ActorAddress )
		{
			Subscribers.emplace( ActorAddress );
		}
		
		inline void RemoveSubscription( const Address & ActorAddress )
		{
			Subscribers.erase( ActorAddress );
		}
		
		// Other methods may need to check the number of subscribers. In particular
		// because the transaction will be deleted if there are no more subscribers.
		
		inline auto NumberOfSubscribers( void )
		{
			return Subscribers.size();
		}
		
		// The constructor requires the session for which the producer and the 
		// consumer will be created. Note that in the case the session layer is 
		// not connected, the session will be undefined. In this case the consumer
		// and producer will also be left undefined. In order to forward inbound 
		// messages to the local subscribers it needs a pointer to the session 
		// layer hosting this transaction.
		
		Transaction ( const Theron::ActiveMQ::Message::Destination Type,
									const std::string & QueueOrTopicName,
									const std::shared_ptr< cms::Session > & Session,
									SessionLayer * Host	);
		
		// The explicit constructor is not allowed
		
		Transaction( void ) = delete;
		
		// The transaction has a copy and move constructor to be used with the 
		// various STL containers
		
		inline Transaction( const Transaction & Other )
		: Producer( Other.Producer ), Consumer( Other.Consumer ),
		  Subscribers( Other.Subscribers ), 
		  ThisSessionLayer( Other.ThisSessionLayer )
		{}

		inline Transaction( const Transaction && Other )
		: Producer( Other.Producer ), Consumer( Other.Consumer ),
		  Subscribers( Other.Subscribers ), 
		  ThisSessionLayer( Other.ThisSessionLayer )
		{}

		// The destructor is virtual to ensure that the class is properly cleared
		// even if it is destructed from a base class pointer.
		
		virtual ~Transaction( void );
	};
	
	// There is a subtle issue with transacted sessions since they need the 
	// commit function to be called after a message is received for it to be 
	// acknowledged by the session layer. The behaviour is meant to support 
	// receiving a batch of messages at once and then when the master message 
	// has been successfully assembled from the individual messages, they are 
	// all committed as one block. The consequence of this is that the session 
	// pointer is needed, and that the onMessage method must be overloaded with 
	// the a version that first calls the standard method and then commits the 
	// reception. Hence the commit will here be done for every single message, 
	// but having a sub-class allows this behaviour to be modified in a later 
	// version.
	
	class CommitTransaction : public Transaction
	{
	private:
		
		std::shared_ptr< cms::Session > Session;
		
	public:
		
		void onMessage( const cms::Message * TheMessage ) override;
		
		inline CommitTransaction ( 
							const Theron::ActiveMQ::Message::Destination Type,
							const std::string & QueueOrTopicName,
							const std::shared_ptr< cms::Session > & TheSession,
							SessionLayer * Host	)
		: Transaction( Type, QueueOrTopicName, TheSession, Host ),
		  Session( TheSession )
		{ }
	};

	// The session is managed by a class that also maintains a map of labels for 
	// topics or queues with the corresponding transactions
	
	class SessionManager
	{
	private:
		
		// The session is created by the constructor when the manager is created.
		
		std::shared_ptr< cms::Session > Session;
		
		// The destination type for this session (queue or topic) is stored to 
		// be able to create the transactions of the right type.
		
		Theron::ActiveMQ::Message::Destination Type;

		// There is a map for the transactions belonging to this session

		std::map< std::string, std::shared_ptr< Transaction > > Topics;
		
		// A pointer is maintained to the session layer so that this can be 
		// forwarded to the transaction object when a transaction object is 
		// needed.
		
		SessionLayer * ThisSessionLayer;
		
	public:
		
		// The constructor needs to know which session type it belongs to, and 
		// it needs the pointer to the connection managed by the session layer
		// in order to create the corresponding session
		
		inline 
		SessionManager( Theron::ActiveMQ::Message::Destination QueueOrTopic,
										const SessionType Mode, 
		 							  const std::shared_ptr< cms::Connection > & Connection,
										SessionLayer * Host	)
		: Session(),Type( QueueOrTopic ), Topics(), ThisSessionLayer( Host )
		{	
/*			if ( Connection )
			 Session = std::make_shared< cms::Session >( 
									 Connection->createSession( 
											 static_cast< cms::Session::AcknowledgeMode >( Mode ) ) );
*/		}
		
		// The default constructor should not be used
		
		SessionManager( void ) = delete;
		
		// However in order to store this in a container a copy constructor and 
		// similar move constructor will be needed
		
		inline SessionManager( const SessionManager & Other )
		: Session( Other.Session ), Type( Other.Type ), Topics( Other.Topics ),
		  ThisSessionLayer( Other.ThisSessionLayer )
		{}

		inline SessionManager( const SessionManager && Other )
		: Session( Other.Session ), Type( Other.Type ), Topics( Other.Topics ),
		  ThisSessionLayer( Other.ThisSessionLayer )
		{}
				
		// The send function takes a message for a topic and sends it. If the 
		// topic does not exist, a temporary producer will be created for the 
		// sending. If the topic already has subscribers, a Transaction will 
		// exist for the topic, and the producer of the transaction will be used,
		// or the producer will be created if it does not exist already. The 
		// function is defined in the source file. The optional flag is used to 
		// indicate that this is the last message in a sequence if the session 
		// is transacted and indicates that the sequence should be committed.
		
		void Send( const std::string & TheQueueOrTopic, 
			         const std::shared_ptr< cms::Message > & TheMessage,
						   const bool CommitMessages = false );
		
		// When an actor subscribes to a topic, its address will be added to 
		// the the relevant transaction. If the transaction does not exist it 
		// will be created, and if the creation fails a standard runtime error 
		// exception will be thrown. The actor's address will be removed from the 
		// transaction when it the subscription is removed, and when the last 
		// subscription is removed, the transaction and the topic will be removed.
		//
		// The function to add a subscription is virtual since it is responsible 
		// for instantiating the Transaction class, and if a different version of 
		// the transaction is needed in order to transform the messages received 
		// in its onMessage function to support special message processing.
		
		virtual void AddSubscription( const std::string & TheQueueOrTopic, 
																	const Address & TheActor );
		
		void RemoveSubscription( const std::string & TheQueueOrTopic, 
														 const Address & TheActor );
	};
	
	// There can be sessions for all of the four combinations: Transacted queue,
	// transacted topic, acknowledged queue, and acknowledged topic. To east the 
	// syntax and provide these "on demand" the different sessions are kept in 
	// a dual level map.
	
	std::map< SessionType, 
		std::map< Theron::ActiveMQ::Message::Destination, SessionManager > >
			Session;

	// ---------------------------------------------------------------------------
  // Topic or queue subscriptions
  // ---------------------------------------------------------------------------
	// 
	// When an actor wants to subscribe to an AMQ topic, it needs to define the 
	// session type, the destination type - queue or topic, and the textual ID 
	// of the topic. If there is no session manager it will be created, and the 
	// actor subscribed to the topic.
			
public:
	
	class Subscription
	{
	public:
		
		const SessionType Type;
		const ActiveMQ::Message::Destination DestinationType;
		const std::string QueueOrTopic;
		
		Subscription( const SessionType TheSessionType, 
									const ActiveMQ::Message::Destination TheDestinationType, 
								  const std::string TheTopic )
		: Type( TheSessionType ), DestinationType( TheDestinationType ),
		  QueueOrTopic( TheTopic )
		{}
		
		Subscription( void ) = delete;
	};
	
	// The handler for this message will ensure that both the session and the 
	// topic subscribed to will exist when it terminates.
	
private:
	
	void AddSubscription( const Subscription & TheSubscription, 
												const Theron::Address TheSubscriber );
	
	// In the same way, when the actor no longer wants to subscribe to a topic,
	// it cancels the subscription, using essentially the same message defined 
	// as a separate class to enable a separate handler for it.
	
public:
	
	class CancelSubscription : public Subscription
	{
	public:
		
		CancelSubscription( const SessionType TheSessionType, 
												const ActiveMQ::Message::Destination TheDestinationType, 
											  const std::string TheTopic )
		: Subscription( TheSessionType, TheDestinationType, TheTopic )
		{}
		
		CancelSubscription( void ) = delete;
	};
	
	// There may be incoming messages on a subscription flowing between the time 
	// when an actor asks to unsubscribe and when the message has been handled. 
	// Thus, the actor must remain available to handle the inbound messages while 
	// waiting for the cancellation, and it is therefore necessary to confirm the 
	// cancellation.
	
	class ConfirmCancellation
	{
	public:
		
		const std::string QueueOrTopic;
		
		ConfirmCancellation( const std::string TheTopic )
		: QueueOrTopic( TheTopic )
		{}
		
		ConfirmCancellation( void ) = delete;
	};
	
	// The handler for this removes the subscription from the session manager for 
	// this subscription type, and if the session has no subscribers, it will be 
	// deleted.
	
private:
	
	void RemoveSubscription( const CancelSubscription & TheCancellation, 
													 const Theron::Address TheSubscriber );

  // ---------------------------------------------------------------------------
  // Constructor
  // ---------------------------------------------------------------------------
  //
	// The fundamental constructor of the actor simply takes the URI of the 
	// message broker. It also needs the user name and the password for 
	// authenticating with the server, and if the user name is an empty string 
	// the default user is used. Note that the URI can encode that the user 
	// name and the password will be taken from environment variables as 
	// described on [3], in which case the user name should be given as an 
	// empty string. 
	
public:
	
	SessionLayer( const std::string & BrokerURI, const std::string & UserName,
								const std::string & Password = std::string(),
								const std::string & Name = "AMQSessionLayer" );
	
};

/*==============================================================================

 Specialisations for storing the property values in a message

==============================================================================*/
//
// Boolean specialisation

template<>
void Message::Value< bool >::StoreProperty( const std::string & Label, 
																				   cms::Message * TheMessage )
{
	TheMessage->setBooleanProperty( Label, TheValue );
}

// Byte specialisation

template<>
void Message::Value< unsigned char >::StoreProperty( const std::string & Label, 
																										 cms::Message * TheMessage )
{
	TheMessage->setByteProperty( Label, TheValue );
}

// Double specialisation

template<>
void Message::Value< double >::StoreProperty( const std::string & Label, 
																						  cms::Message * TheMessage )
{
	TheMessage->setDoubleProperty( Label, TheValue );
}

// Float specialisation

template<>
void Message::Value< float >::StoreProperty( const std::string & Label, 
																						 cms::Message * TheMessage )
{
	TheMessage->setFloatProperty( Label, TheValue );
}

// Integer specialisation

template<>
void Message::Value< int >::StoreProperty( const std::string & Label, 
																					 cms::Message * TheMessage )
{
	TheMessage->setIntProperty( Label, TheValue );
}

// Long specialisation

template<>
void Message::Value< long long int >::StoreProperty( const std::string & Label, 
																										 cms::Message * TheMessage )
{
	TheMessage->setLongProperty( Label, TheValue );
}

// Short specialisation

template<>
void Message::Value< short int >::StoreProperty( const std::string & Label, 
																								 cms::Message * TheMessage )
{
	TheMessage->setShortProperty( Label, TheValue );
}

// String specialisation

template<>
void Message::Value< std::string >::StoreProperty( const std::string & Label, 
																									 cms::Message * TheMessage )
{
	TheMessage->setStringProperty( Label, TheValue );
}


}      // Name space ActiveMQ
#endif // THERON_ACTIVEMQ_CLIENT
