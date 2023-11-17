/*==============================================================================
Zero Message Queue (ZMQ) Messages

This file implements the functionality of the message class used to exchange
messages over ZMQ with external endpoints.

Author and Copyright: Geir Horn, University of Oslo, 2017-2019
License: LGPL 3.0
==============================================================================*/

#include <map>						              // To match types and strings
#include <algorithm>                    // To search for strings

#include "Communication/ZMQ/ZMQMessages.hpp"

// -----------------------------------------------------------------------------
// Transmitting the outside messages
// -----------------------------------------------------------------------------
//
// First the map between the types of the outside message and the string
// representing the message type is defined as a constant map.

const std::map< std::string, Theron::ZeroMQ::OutsideMessage::Type > TypeMap
= { { "Subscribe", Theron::ZeroMQ::OutsideMessage::Type::Subscribe },
		{ "Discovery", Theron::ZeroMQ::OutsideMessage::Type::Discovery },
	  { "Address",   Theron::ZeroMQ::OutsideMessage::Type::Address   },
		{ "Remove",    Theron::ZeroMQ::OutsideMessage::Type::Remove    },
		{ "Closing",   Theron::ZeroMQ::OutsideMessage::Type::Closing   },
		{ "Roster",    Theron::ZeroMQ::OutsideMessage::Type::Roster    },
		{ "Message",   Theron::ZeroMQ::OutsideMessage::Type::Message   },
		{ "Data",      Theron::ZeroMQ::OutsideMessage::Type::Data      } };

using TypeRecord =
			std::pair< std::string, Theron::ZeroMQ::OutsideMessage::Type >;

// Converting an incoming message to the outside message format is relatively
// easy given that each ZMQ message field corresponds to the parts of the
// outside message. The fields are laid out in the following order:
//	1. Type (or data in case of message received from a different sensor)
//  2. From address
//  3. To address
//  4. Payload (optional if command message)

Theron::ZeroMQ::OutsideMessage
Theron::ZeroMQ::OutsideMessage::Extract( zmqpp::message & ReceivedMessage )
{
	std::string MessageField;

	ReceivedMessage >> MessageField;

	auto TheType = TypeMap.find( MessageField );

	// The other message fields must be defined by their default interpretation

	Type MessageType;
	NetworkAddress SenderAddress, ReceiverAddress;
	SerialMessage::Payload ThePayload;

	// If the given field was not recognised as a valid command, it should be
	// treated as the payload and the type should be set to Data. The sender and
	// receiver must be defined by another mechanism.

	if ( TheType == TypeMap.end() )
  {
		MessageType = Type::Data;
		ThePayload  = MessageField;
	}
	else
  {
		MessageType = TheType->second;
		ReceivedMessage >> SenderAddress >> ReceiverAddress >> ThePayload;
	}

	// Finally the outside message can be created

	return
	OutsideMessage( MessageType, SenderAddress, ReceiverAddress, ThePayload );
}

// The utility function doing the reverse lookup is implemented as a search
// for the right binary type in the second field of the map. This has linear
// complexity, but the comparison is simpler than comparing strings, and the
// number of possible commands is not too many. However, one may use a second
// map defined with the typed enumerations as key at the expense of using more
// memory and having to maintain the two maps consistently; or even Boost
// bimap although this possibly comes with a complexity overhead too.

std::string Theron::ZeroMQ::OutsideMessage::Type2String( void ) const
{
	auto TheType = std::find_if( TypeMap.begin(), TypeMap.end(),
													     [this]( const TypeRecord & Command )->bool{
																	 return Command.second == MessageType; });

	// It should be noted that since this is done for a message the type must
	// exist and have a legal value, and the find will succeed. Hence it is just
	// to return the result of the lookup.

	return TheType->first;
}

// Converting the outside message to a set of ZMQ frames is slightly easier
// since all fields does exist in this case.

void Theron::ZeroMQ::OutsideMessage::Send( zmqpp::socket & NetworkSocket ) const
{
	zmqpp::message NetworkMessage;

	NetworkMessage << Type2String() << SenderAddress << ReceiverAddress
								 << Payload;

	NetworkSocket.send( NetworkMessage );
}

