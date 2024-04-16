/*==============================================================================
Active Message Queue (AMQ) JSON messages

The JavaScript Object Notation (JSON) is a much used standard for information 
exchange supporting simple values, recursive key-value maps and arrays [1]. 
The same structure is provided by Qpid Proton AMQ interface [2]. Thus, the 
standard way of communication JSON structures over the network is to searialise 
the message to a simple string and the recreate the JSON structure at the 
receiver parsing the syntax of the received string.

However, given that the Qpid Proton AMQ interface mirrors the data types of 
JSON, the purpose of this class is to create an AMQ message from the JSON 
structure, and then leave the potential serialisation to the Network Layer. 
For this the Niels Lohmann's JSON library will be used as it is written 
with a modern C++ interface, and an interface simlar Standard Template 
library (STL) [2].

References: 
[1] https://www.iso.org/standard/71616.html
[2] https://github.com/nlohmann/json

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
==============================================================================*/

#ifndef THERON_AMQ_JSON_MESSAGE
#define THERON_AMQ_JSON_MESSAGE

// Standard headers


// Niels Lohmann's JSON library

#include <nlohmann/json.hpp>
using JSON = nlohmann::json;

// The Qpid Proton headers

#include "proton/message.hpp"
#include "proton/map.hpp"

// The Theron++ messages

#include "Communication/PolymorphicMessage.hpp"
#include "Communication/NetworkEndpoint.hpp" 
#include "Communication/AMQ/AMQMessage.hpp"
#include "Communication/AMQ/AMQEndpoint.hpp"

// Debugging

#include "Utility/ConsolePrint.hpp"

namespace Theron::AMQ
{

/*==============================================================================

 JSON Message

==============================================================================*/

class JSONMessage
: virtual public Theron::PolymorphicMessage<typename Theron::AMQ::Message::PayloadType>,
  virtual public JSON
{
 public:

    using ProtocolPayload = PolymorphicMessage< 
          typename Theron::AMQ::Message::PayloadType >::PayloadType;

  // JSON messages must have a unique message identifier string. This will be
  // embedded in the message content type so that the message can be correctly
  // decoded by the receiver. The issue is that when the AMQ message is 
  // unpacked to a JSON message, one will end up with an attribute-value
  // map for all messages, but and one will need to test all the attributes 
  // to see if the JSON message structure is as expected. However, having 
  // the unique identifier available, this can be tested in the overloaded 
  // initialisation function to validate that the message is as expeced by 
  // a derived JSON message type.

private:

  const std::string UniqueMessageIdentifier;

  // There is a read-only function to allow derived classes to introspect
  // and check the message identifier.

protected:

  inline const std::string & GetMessageIdentifier( void ) const
  { return UniqueMessageIdentifier; }

  // Taking full adavantage of the structured AMQ message implies that the 
  // JSON structure itself is represented as a map from strings to AMQ values, 
  // where value can again be an object owing to the recursive nature of the 
  // JSON stricture. It is therefore a dedicated function to convert a
  // JSON value to a AMQ Proton value.

private:

  proton::value Json2AMQValue( const JSON & TheValue ) const;

  // In the same way, the AMQ message can also be structured, and each value
  // must be correctly decoded to a proper JSON value. 
  
  JSON AMQValue2Json( const proton::value & TheValue ) const;

protected:

  virtual ProtocolPayload GetPayload( void ) const override;
  virtual bool 
  Initialize( const ProtocolPayload & ThePayload ) noexcept override;
  virtual Address PresentationLayerAddress( void ) const override;
  
public:

  // The constructor requires the unique message identifier to recognise 
  // the incoming message types by the content type field set.

  JSONMessage( const std::string_view & TheMessageID )
  : PolymorphicMessage(), JSON(), 
    UniqueMessageIdentifier( static_cast< std::string >( TheMessageID ) )
  {}

  // The default constructor not allowed 

  JSONMessage() = delete;
  
  // The copy constructor is much more complicated since structured objects
  // cannot be directly copied. The to_json and from_json methods works only
  // if the structure of the message is known and cannot be used in general.
  // The only solution found is to serialise the message and then parse it 
  // back to the new object. This is largely undocumented feature, but this
  // wasteful operation works also for messages with complex structures.

  JSONMessage( const JSONMessage & Other )
  : PolymorphicMessage(), JSON(), 
    UniqueMessageIdentifier( Other.UniqueMessageIdentifier )
  {
    this->JSON::operator=( parse( Other.dump() ) );
  }

  // Since the JSON object accepts being initialised, there is a constructor
  // to forward the initialiser type. Even though it compiles to copy construct
  // the JSON part of the message, the JSON data is not copied to the current 
  // JSON message. However, the equal operator works well, and it is therefore
  // used as a backup.

  JSONMessage( const std::string_view & TheMessageID, 
               const JSON & JSONData )
  : PolymorphicMessage(), JSON(), 
    UniqueMessageIdentifier( static_cast< std::string >( TheMessageID ) )
  {
    this->JSON::operator=( JSONData );
  }

  // The destructor is just auto generated by the compiler.

  virtual ~JSONMessage() = default;
};

/*==============================================================================

 Topic message

==============================================================================*/
//
// JSON messages sent to topics are destinguished only by the name of the topic,
// but the topic name is not a part of the JSON message, and so the initialiser
// function decoding the message based on the unique message identifier must 
// see the sender. The solution to this is to provide an extension to the 
// JSON message that assumes that will compare the sender address of the 
// AMQ message payload with the unique message idenfier and if the two matches,
// the content type of the payload will be set to the message identifier and 
// the message will be forwarded for decoding using the normal initialiser 
// function above.

class JSONTopicMessage
: virtual public JSON,
  public JSONMessage
{
protected: 

  virtual bool 
  Initialize( const ProtocolPayload & ThePayload ) noexcept override
  {
    if( ThePayload->reply_to() == GetMessageIdentifier() )
    {
      ThePayload->content_type( GetMessageIdentifier() );
      return JSONMessage::Initialize( ThePayload );
    }
    else return false;
  }

public:

  JSONTopicMessage( const std::string_view & TheTopicName )
  : JSON(), JSONMessage( TheTopicName )
  {}

  JSONTopicMessage( const std::string_view & TheTopicName, 
                    const JSON & JSONData )
  : JSON(), JSONMessage( TheTopicName, JSONData )
  {}

  JSONTopicMessage( const JSONTopicMessage & Other )
  : JSON(), JSONMessage( Other )
  {}

  JSONTopicMessage() = delete;
  
  virtual ~JSONTopicMessage() = default;
};

/*==============================================================================

 Wildcard topic message

==============================================================================*/
//
// There are many situations where one would like a binary message type to 
// be the same for a set of topics starting with the same root. Typically, 
// this is where one would subscribe to sensor values and where all the sensors 
// use the same JSON format and the identifier of the sending sensor will be 
// a part of the message format. In this case one would like messages from 
// all the different topics to be handled in a uniform way and create one single
// message handler for the messages from all sensors. In this case, the 
// requirement that the sender equals the topic cannot be used because one 
// would then need one message for each sensor. However, if the topics have 
// the same root topic names, then one may let the message identifier match 
// the first part of the string, like "value.for.sensor.X" where "X" is the 
// unique name for a particular sensor. Then the unique message identifier 
// could be "value.for.sensor" and thereby catch all published sensor values.

class JSONWildcardMessage
: public JSONMessage
{
protected:

  virtual bool 
  Initialize( const ProtocolPayload & ThePayload ) noexcept override
  {
    if( ThePayload->reply_to().starts_with( GetMessageIdentifier() ) )
    {
      ThePayload->content_type( GetMessageIdentifier() );
      return JSONMessage::Initialize( ThePayload );
    }
    else return false;
  }

public:

  JSONWildcardMessage( const std::string_view & TopicIdentifier )
  : JSONMessage( TopicIdentifier )
  {}

  JSONWildcardMessage( const std::string_view & TopicIdentifier, 
                       const JSON & JSONData )
  : JSONMessage( TopicIdentifier, JSONData )
  {}
  
  JSONWildcardMessage( const JSONWildcardMessage & Other )
  : JSONMessage( Other.GetMessageIdentifier(), Other )
  {}

  JSONWildcardMessage() = delete;
  virtual ~JSONWildcardMessage() = default;
};

}      // end namespace Theron::AMQ
#endif // THERON_AMQ_JSON_MESSAGE