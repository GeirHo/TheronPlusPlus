/*=============================================================================
  Presentation Layer

  A message must be a Polymoephic Message for it to be able to be sent across 
	the network. In terms of this framework it means that the message must 
	inherit the Polymoephic Message base class, and implement the virtual 
	function GetPayload() from that base class. It will be called by the
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
  Presentation Layer server will then call the GetPayload() method on the 
	message to obtain the representation of the message to send to the remote
  actor C.

  After packing the message up as and external payload, the string will be 
	forwarded to the Session Layer server which will embed the message as the 
	payload of a message in the right protocol, before the Session Layer delivers
	the message to the Network Layer for actual transmission.

  The reverse process is more complex because the only information available
  to the Session Layer is the actor identifier of the remote sender, the actor
	identifier of the local actor to receive the message, and an external payload
	representing the message content. The two actors involved can exchange many 
	different types of messages, and there is no way for the Presentation Layer 
	automatically to deduce which binary message the payload should be converted
	into. The application developer must therefore provide a message handler 
	capable of receiving a Polymorphic Message on each actor class that receives
	message. This message handler must convert the payload it receives back to 
	the right binary message format and call the actor's message handler for 
	this binary message.

  To continue the above example: When a message arrives from C, the 
	Presentation Layer will receive a network message with C as the sender and, 
	say, B as the receiver. The payload will be stored as a Payload and forwarded
  to B with C as the sender, and B's handler for external payloads will receive
  the message, convert it to the right binary format, and resend the message to
  B's handler for the given binary message type.

  REVISION: This file is NOT compatible with standard Theron - the new actor
            implementation of Theron++ MUST be used since the original 
						version of this class only supported serialisation of messages.
						The current version lets the polymorphic message convert the binary
						message to any kind of message that is understood by the network
						layer protocol.

  Author and Copyright: Geir Horn, University of Oslo
  Contact: Geir.Horn@mn.uio.no
  License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
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
#include <memory>
#include <source_location>	

#include "Actor.hpp"
#include "Utility/StandardFallbackHandler.hpp"
#include "Communication/NetworkEndpoint.hpp"
#include "Communication/PolymorphicMessage.hpp"

// The Presentation Layer is defined to be a part of the Theron name space

namespace Theron
{
template< class ProtocolPayload >
class PresentationLayer : virtual public Actor,
													virtual public StandardFallbackHandler
{
  // --------------------------------------------------------------------------
  // Shut down management
  // --------------------------------------------------------------------------
  //
	// When the system is shutting down, the de-serializing actors should be
	// blocked from sending more messages to the remote actors. This is done by
	// a shut down message from the session layer, that sets a flag that
	// prevents further messages.

private:

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
  public:

    const Address From, To;
    const ProtocolPayload MessagePayload;

  public:

    RemoteMessage( const Address & TheSender, const Address & TheReceiver,
				   				 const ProtocolPayload & ThePayload )
    : From( TheSender ), To( TheReceiver ), MessagePayload( ThePayload )
    {};
		
		RemoteMessage( const RemoteMessage & Other )
		: RemoteMessage( Other.From, Other.To, Other.MessagePayload )
		{};

		RemoteMessage()  = delete;
		~RemoteMessage() = default;
  };

	// Since this is a part of the internal protocol with the Session Layer server
	// only this can access this definition. However since the session layer is
	// a template class, the friend declaration must also be a template.

	template< class ExternalMessage >
	friend class SessionLayer;

  // --------------------------------------------------------------------------
  // Message encoding
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
	// case the message should be a polymorphic message, and in the inbound case 
	// it should be a Remote Message coming from the session layer.
	//
	// The implementation is therefore based on the Run Time Type Information
	// (RTTI) and the ability to convert the generic message to a message of the
	// expected type. Invalid argument exceptions will be created if the message
	// given is not of the expected type.

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
		  // The message will simply be ignored if the network stack is shutting
			// down and the forward flag has been cleared.

			if ( ForwardOutboundMessages )
		  {
				// The outbound message should in this case be polymorphic, and
				// the payload is created first.

				std::shared_ptr< Theron::PolymorphicProtocolHandler > 
				OutboundMessage( TheMessage->GetPolymorphicMessagePointer() );

				if ( OutboundMessage )
				{
					// At this point there are two cases to consider. The message could
					// have this presentation layer as its presentation layer, which
					// means that it supports the network protocol served by this
					// presentation layer, and it can be readily forwarded to the
					// session layer as a remote message.

					Address HandlingLayer( OutboundMessage->PresentationLayerAddress() );

					if ( HandlingLayer == GetAddress() )
					{
						// The message is then cast to the expected polymorphic message 
						// class based on the payload type of this presentation layer
						
						std::shared_ptr< PolymorphicMessage< ProtocolPayload > >
						ConvertableMessage = std::dynamic_pointer_cast< 
										PolymorphicMessage< ProtocolPayload > >( OutboundMessage );
										
						// If the conversion was successful the payload can be encoded and 
						// the corresponding remote message sent to the session layer for 
						// address mapping to the global addresses and subsequent network 
						// transmission by the Network Layer. However, if the conversion 
						// was unsuccessful, it means that the message type cannot be 
						// handled by this presentation layer, and there is a mismatch 
						// between the information provided by the presentation layer 
						// address and the polymorphic message payload. There is no way 
						// to recover from this logic error, and a standard exception is 
						// thrown.
						
						if ( ConvertableMessage )
							Send( RemoteMessage( TheMessage->From, TheMessage->To,
																	 ConvertableMessage->GetPayload() ),
										Network::GetAddress( Network::Layer::Session ) );
						else
						{
							std::ostringstream ErrorMessage;
							std::source_location Location = std::source_location::current();
							
							ErrorMessage << Location.file_name() << " at line " 
													 << Location.line() << ": The Presentation Layer "
													 << "server " << GetAddress().AsString() 
													 << " in function " << Location.function_name()
													 << " got a message with incompatible payload type"
													 << OutboundMessage->GetProtocolTypeName();
													 
						  throw std::invalid_argument( ErrorMessage.str() );
						}
					}
					else
				  {
						// This was the first of multiple presentation layer servers
						// and it has been given the responsibility to forward messages
						// to the other presentation layers. It will do this via the
						// delegated Enqueue function.

						Actor::EnqueueMessage( HandlingLayer, TheMessage );
					}
				}
				else
				{
					std::ostringstream ErrorMessage;
					std::source_location Location = std::source_location::current();

					ErrorMessage << Location.file_name() << " at line " 
											 << Location.line() 
											 << " in function " << Location.function_name() << ": "
											 << "Outbound message of type " 
											 << TheMessage->GetMessageTypeName() 
											 << " to the Presentation Layer " 
											 << GetAddress().AsString() <<  "from Actor "
											 << TheMessage->From.AsString() << " with receiver Actor "
											 << TheMessage->To.AsString()
											 << " is not a polymorphic message";

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
  // Constructor and destructor
  // --------------------------------------------------------------------------
	//
	// The constructor registers the handler for the incoming messages and the
  // default handler.

  PresentationLayer( const std::string & ServerName = "PresentationLayer"  )
  : Actor( ServerName ),
    StandardFallbackHandler( Actor::GetAddress().AsString() ),
		ForwardOutboundMessages( true )
  {
		RegisterHandler( this, &PresentationLayer::Stop );
		Actor::SetPresentationLayerServer( GetAddress() );
  }

  // There is no default constructor or copy constructor.

	PresentationLayer() = delete;
	PresentationLayer( const PresentationLayer & Other ) = delete;

	// The virtual destructor is just the default destructor.

	virtual ~PresentationLayer() = default;
};

} 			// End of name space Theron
#endif 	// THERON_PRESENTATION_LAYER
