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
#include "Utility/StandardFallbackHandler.hpp"
#include "Communication/NetworkEndpoint.hpp"
#include "Communication/SerialMessage.hpp"

// The Presentation Layer is defined to be a part of the Theron name space

namespace Theron
{

class PresentationLayer : virtual public Actor,
													virtual public StandardFallbackHandler
{
private:

  // --------------------------------------------------------------------------
  // Shut down management
  // --------------------------------------------------------------------------
  //
	// When the system is shutting down, the de-serializing actors should be
	// blocked from sending more messages to the remote actors. This is done by
	// a shut down message from the session layer, that sets a flag that
	// prevents further messages.

	bool ForwardOutboundMessages;

	// The handler for the shut down message simply sets this flag to false

	void Stop( const Network::ShutDown & StopMessage, const Address Sender )
	{	ForwardOutboundMessages = false; }

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

	// Since this is a part of the internal protocol with the Session Layer server
	// only this can access this definition. However since the session layer is
	// a template class, the friend declaration must also be a template.

	template< class ExternalMessage >
	friend class SessionLayer;

  // --------------------------------------------------------------------------
  // Serialisation and de-serialisation
  // --------------------------------------------------------------------------
	//
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
	// it should be a Remote Message coming from the session layer.
	//
	// The implementation is therefore based on the Run Time Type Information
	// (RTTI) and the ability to convert the generic message to a message of the
	// expected type. Invalid argument exceptions will be created if the message
	// given is not of the expected type. Note that the message will not be
	// enqueued for processing with this actor.

protected:

	virtual
	bool EnqueueMessage( const std::shared_ptr< GenericMessage > & TheMessage )
	override
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
		  // sender. Otherwise, the message should be treated as a normal message
		  // to this presentation layer actor.

		  if ( InboundMessage )
				Send( InboundMessage->TheMessage->GetPayload(),
							InboundMessage->TheMessage->GetSender(),
							InboundMessage->TheMessage->GetReceiver() );
			else
				Actor::EnqueueMessage( TheMessage );
		}
		else
		{
			// The outbound message should in this case support serialisation, and
			// the payload is created first.

			SerialMessage * OutboundMessage( TheMessage->GetSerialMessagePointer() );

		  // A valid message will in this case be forwarded as a remote message
		  // to the Session Layer server.

			if ( ForwardOutboundMessages )
		  {
				if ( OutboundMessage != nullptr )
					Send( RemoteMessage( TheMessage->From, TheMessage->To,
															 OutboundMessage->Serialize() ),
								Network::GetAddress( Network::Layer::Session ) );
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
	//
	// The constructor registers the handler for the incoming messages and the
  // default handler. The Session Layer server address is initialised with the
  // default address of the Session Layer. This is possible since a Theron
  // Address does not check that the actor exists when it is constructed on
  // a string. The check is only done when the fist message is sent to this
  // address. Hence, as long as the default names are used for the actors,
  // then no further initialisation is needed.

  PresentationLayer( const std::string & ServerName = "PresentationLayer"  )
  : Actor( ServerName ),
    StandardFallbackHandler( Actor::GetAddress().AsString() ),
		ForwardOutboundMessages( true )
  {
		RegisterHandler( this, &PresentationLayer::Stop );
		Actor::SetPresentationLayerServer( this );
  }

  // The compatibility constructor accepts a pointer to the framework which
  // in classical Theron should be the same as the network end point. It just
  // delegates to the above constructor and forgets about the framework pointer.

  PresentationLayer( Framework * TheHost,
								     const std::string ServerName = "PresentationLayer"  )
  : PresentationLayer( ServerName )
  { }
};

} 			// End of name space Theron
#endif 	// THERON_PRESENTATION_LAYER
