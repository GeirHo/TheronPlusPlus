/*==============================================================================
Zero Message Queue (ZMQ) Messages

The outside message must minimally contain the sender address, the receiver
address and the serialised payload. The interface is defined by the link
message class, and it based on the ZMQ network address for supporting the
external address.

Author and Copyright: Geir Horn, University of Oslo, 2017-2019
License: LGPL 3.0
==============================================================================*/

#ifndef THERON_ZMQ_MESSAGES
#define THERON_ZMQ_MESSAGES

#include <zmqpp.hpp>				 	                 // ZeroMQ bindings for C++

#include "Actor.hpp"                           // For Theron++ Actor addresses

#include "Communication/LinkMessage.hpp"       // Generic link messages
#include "Communication/SerialMessage.hpp"     // Network messages
#include "Communication/ZMQ/ZMQAddress.hpp"    // The address format

namespace Theron::ZeroMQ
{
class OutsideMessage : public LinkMessage< NetworkAddress >
{
public:

  // ---------------------------------------------------------------------------
  // Message types
  // ---------------------------------------------------------------------------
  //
	// The outside message will be interpreted by the remote network layer actor
	// and processed accordingly. It is transmitted as the first frame of the
	// ZMQ message, and if the first frame does not decode to one of the types,
	// it is taken to be the payload and the message type is set to Data.

	enum class Type
	{
		Subscribe,				// Ask all other endpoints to subscribe to a new peer
		Discovery,        // Start an address resolution for an actor
		Address,					// Provide an address for an actor
		Remove,           // An actor is being removed from the system
		Closing,          // The last message from a closing endpoint
		Roster,						// IP addresses of the peers to subscribe to
		Message,					// Normal message between two actors
		Data							// Raw data
	};

  // ---------------------------------------------------------------------------
  // Message content
  // ---------------------------------------------------------------------------

private:

	Type MessageType;

  // ---------------------------------------------------------------------------
  // Interface functions
  // ---------------------------------------------------------------------------

public:

	// Exporting the message address type

	using LinkMessage< NetworkAddress >::AddressType;

	// Obtaining the type of the message

	inline Type GetType( void ) const
	{ return MessageType; }

	// There must also be a function to convert the external address to an actor
	// address. This is just to return the actor address as returned by the
	// network address itself.

	virtual
	Address ActorAddress(const NetworkAddress & ExternalActor) const override
	{
		return ExternalActor.GetActorAddress();
	}

  // ---------------------------------------------------------------------------
  // Constructors & destructor
  // ---------------------------------------------------------------------------

	inline OutsideMessage( Type Category, const NetworkAddress & From,
												 const NetworkAddress & To,
												 const SerialMessage::Payload & ThePayload
														 = SerialMessage::Payload() )
	: LinkMessage< NetworkAddress >( From, To, ThePayload ),
	  MessageType( Category )
	{ }

	// Copy, move and assign from other outside messages is just invoking the
	// standard constructor.

	inline OutsideMessage( const OutsideMessage & Other )
	: OutsideMessage( Other.MessageType, Other.SenderAddress,
										Other.ReceiverAddress, Other.Payload )
	{ }

	inline OutsideMessage( const OutsideMessage && Other )
	: OutsideMessage( Other.MessageType, Other.SenderAddress,
										Other.ReceiverAddress, Other.Payload )
	{ }

	// There is a reduced compatibility constructor to have the same parameters
	// as for the standard link message operator. In this case the message type
	// will be set to "Message"

	OutsideMessage( const NetworkAddress & From, const NetworkAddress & To,
									const SerialMessage::Payload & ThePayload
									    = SerialMessage::Payload() )
	: OutsideMessage( Type::Message, From, To, ThePayload )
	{ }

	// The default constructor is used when delayed initialisation through the
	// function operator is used. The Session Layer server does this when sending
	// messages to the link layer.

	OutsideMessage( void ) = delete;

	// Since the message contains virtual functions it needs a virtual destructor

	virtual ~OutsideMessage( void )
	{ }

  // ---------------------------------------------------------------------------
  // Transmitting the message over the network
  // ---------------------------------------------------------------------------
	//
	// It is more interesting that the message can be completely constructed from
	// a ZMQ message, which is used when receiving the message. There is an
	// extractor function for this purpose creating an outside message. Since this
	// is a new message, the extractor can be used without any outside message
	// object.

	static OutsideMessage Extract( zmqpp::message & ReceivedMessage );

	// This allows a constructor to be defined that first creates a temporary
	// message which will then be moved to the object being constructed.

	inline OutsideMessage( zmqpp::message & ReceivedMessage )
	: OutsideMessage( Extract( ReceivedMessage) )
	{}

	// There is a send function taking a sending socket as argument sending the
	// message as a multi-part message.

	void Send( zmqpp::socket & NetworkSocket ) const;

	// When sending the message over the network it is necessary to encode the
	// message type. This is done as a string.

	std::string Type2String( void ) const;

}; // End outside message

}      // End name space Theron::ZeroMQ
#endif // THERON_ZMQ_MESSAGES
