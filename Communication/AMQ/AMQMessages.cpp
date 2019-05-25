/*==============================================================================
Active Message Queue: Messages

The Active Message Queue (AMQ) [1] is a server based messaging system where
various clients can exchange messages via a server (message broker) using
the following two models of communication. See the ActiveMQ header file for
details on implementation.

This file defines the message format between the session layer and the network
layer and the global addresses used for actors.

References:

[1] http://activemq.apache.org/

Author and Copyright: Geir Horn, University of Oslo, 2019
License: LGPL 3.0
==============================================================================*/

#include "Actor.hpp"                         // Basic Theron++ definitions
#include "Communication/AMQ/AMQMessages.hpp" // Interface definition

// CMS headers

#include <cms/TextMessage.h>
#include <cms/ObjectMessage.h>
#include <cms/MapMessage.h>
#include <cms/BytesMessage.h>

/*==============================================================================

 Messages

==============================================================================*/
//
// Dealing with the address conversion is trivial in this case where the
// sender address simply corresponds to the actor address.

Theron::Address Theron::ActiveMQ::Message::ActorAddress (
  const Theron::ActiveMQ::GlobalAddress & ExternalActor ) const
{
	return ExternalActor.ActorAddress();
}

// The properties of the message can be transferred to the CMS message before
// it is transmitted. To avoid double storage of the strings identifying the
// sender actor and the receiving actor, they are treated separately.

void
Theron::ActiveMQ::Message::StoreProperties( cms::Message * TheMessage ) const
{
	for ( auto & TheProperty : Properties )
		TheProperty.second->StoreProperty( TheProperty.first, TheMessage );
}

// Getting properties from a CMS message is basically testing repeatedly for
// the type of the property and then store it locally.

void Theron::ActiveMQ::Message::GetProperties( const cms::Message * TheMessage )
{
	std::vector< std::string > Labels( TheMessage->getPropertyNames() );

	// The properties are set according to the type identified for the property
	// in the CMS message.

	for ( std::string & Label : Labels )
		switch ( TheMessage->getPropertyValueType( Label ) )
		{
			case cms::Message::BOOLEAN_TYPE:
				SetProperty( Label, TheMessage->getBooleanProperty( Label ) );
				break;
			case cms::Message::BYTE_TYPE:
				SetProperty( Label, TheMessage->getByteProperty( Label ) );
				break;
			case cms::Message::CHAR_TYPE:
				SetProperty( Label, TheMessage->getByteProperty( Label ) );
				break;
			case cms::Message::SHORT_TYPE:
				SetProperty( Label, TheMessage->getShortProperty( Label ) );
				break;
			case cms::Message::INTEGER_TYPE:
				SetProperty( Label, TheMessage->getIntProperty( Label ) );
				break;
			case cms::Message::LONG_TYPE:
				SetProperty( Label, TheMessage->getLongProperty( Label ) );
				break;
			case cms::Message::DOUBLE_TYPE:
				SetProperty( Label, TheMessage->getDoubleProperty( Label ) );
				break;
			case cms::Message::FLOAT_TYPE:
				SetProperty( Label, TheMessage->getFloatProperty( Label ) );
				break;
			case cms::Message::STRING_TYPE:
				SetProperty( Label, TheMessage->getStringProperty( Label ) );
				break;
			default:
		  {
				std::ostringstream ErrorMessage;

				ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									   << "Property type numeration "
										 << TheMessage->getPropertyValueType( Label )
										 << " is unknown and unhanded";

			  throw std::logic_error( ErrorMessage.str() );
			}
		}
}

/*==============================================================================

 Text Messages

==============================================================================*/
//
// The validation checks if the message pointer is not a null pointer, and
// throws an invalid argument if the pointer is null.

const cms::TextMessage * Theron::ActiveMQ::TextMessage::Validate(
	cms::TextMessage* TheMessage )
{
	if ( TheMessage == nullptr )
	{
		std::ostringstream ErrorMessage;

		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
		             << "A NULL pointer was given as an AMQ text message pointer";

	  throw std::invalid_argument( ErrorMessage.str() );
	}
	else return TheMessage;
}

// and
// then tries to convert it to a text message. If the conversion fails, then
// an invalid argument exception is thrown if the pointer is null, and a
// runtime error is thrown if the pointer is not a text message.

const cms::TextMessage *
Theron::ActiveMQ::TextMessage::Validate( const cms::Message * TheMessage)
{
	const cms::TextMessage *
  ValidPointer = dynamic_cast< const cms::TextMessage * >( TheMessage );

	if ( ValidPointer == nullptr )
	{
		std::ostringstream ErrorMessage;

		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
		             << "Theron++ AMQ Text message error: ";

	  if ( dynamic_cast< const cms::StreamMessage * >( TheMessage ) != nullptr )
			ErrorMessage << " a Stream Message ";
		else
		if ( dynamic_cast< const cms::ObjectMessage * >( TheMessage ) != nullptr )
			ErrorMessage << " an Object Message ";
		else
		if ( dynamic_cast< const cms::MapMessage * >( TheMessage ) != nullptr )
			ErrorMessage << " a Map Message ";
		else
		if ( dynamic_cast< const cms::BytesMessage * >( TheMessage ) != nullptr )
			ErrorMessage << " a Byte Message ";

		ErrorMessage << "was received instead of a text message.";
		throw std::runtime_error( ErrorMessage.str() );
	}

	return ValidPointer;
}

// The constructor for text messages based on a CMS text message pointer will
// decode the message and throw an invalid argument exception if the pointer
// given is a null pointer and not useful for object access. If the first
// validation passes, then the pointer is valid also for the subsequent use.

Theron::ActiveMQ::TextMessage::TextMessage(
	const cms::TextMessage * TheTextMessage )
: Message( GlobalAddress(
			   Validate( TheTextMessage )->getStringProperty("SendingActor") ),
		GlobalAddress( TheTextMessage->getStringProperty("ReceivingActor") ),
		TheTextMessage->getText(),
		Type::TextMessage,
		Message::GetDestination( TheTextMessage ) )
{
	GetProperties( TheTextMessage );
}
