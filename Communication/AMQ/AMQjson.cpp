/*=============================================================================
Active Message Queue (AMQ) JSON messages

This file implements the mapping functions from the JSON structure to the the
requirements of the AMQ Proton message. See the header file for details on the 
interface.

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
=============================================================================*/

// Standard headers

#include <string>           // For standard strings
#include <vector>           // Standard vectors to store arrays
#include <memory>           // For smart pointers
#include <source_location>  // Making informative error messages
#include <sstream>          // Stream based strings
#include <stdexcept>        // standard exceptions
#include <unordered_map>    // For JSON attribute-value objects

// AMQ Proton headers

#include "proton/types.hpp"     // Compound type definitions
#include "proton/message.hpp"   // The AMQ message
#include "proton/map.hpp"       // The AMQ map structure

// Theron++ communication headers

#include "Communication/AMQ/AMQjson.hpp"

namespace Theron::AMQ
{

/*=============================================================================

  Converting protocol values

=============================================================================*/
//
// Owing to the recursive nature of both the JSON structure and the AMQ message
// the values contained in the structure messages are decoded individually so 
// that the message structure can be recursively decoded.
//
// ----------------------------------------------------------------------------
// Converting to AMQ values
// ----------------------------------------------------------------------------
//
// The simple values may readily be converted, and the only complicated types 
// are the array, representing a sequence of value objects where the index 
// is the place in the sequence, and the JSON "object" essentially being the
// attribute-value pairs indext by the attribute string.

proton::value JSONMessage::Json2AMQValue( const JSON & TheValue ) const
{
  switch( TheValue.type() )
  {
    case JSON::value_t::null:
    case JSON::value_t::discarded:
      return proton::value();
    case JSON::value_t::boolean:
      return static_cast<bool>( TheValue );
    case JSON::value_t::string:
      return std::string( TheValue );
    case JSON::value_t::number_integer:
      return static_cast< int64_t >( TheValue );
    case JSON::value_t::number_unsigned:
      return static_cast< uint64_t >( TheValue );
    case JSON::value_t::number_float:
      return static_cast< double >( TheValue );
    case JSON::value_t::binary:
      // The binary type is stored internally as a standerd vector 
      // of unsigned int 8 bits long. It is stored as a binary AMQ 
      // object.
      return proton::binary( TheValue.get_binary() );
    case JSON::value_t::array:
    {
      std::vector< proton::value > AMQValue;
      
      for( const auto & TheJSONValue : TheValue.items() )
        AMQValue.emplace_back( Json2AMQValue( TheJSONValue.value() ) );

      return AMQValue;
    }    
    case JSON::value_t::object:
    {
      std::unordered_map< std::string, proton::value > AMQValue;

      for( const auto & TheJSONValue : TheValue.items() )
        AMQValue.emplace( TheJSONValue.key(), 
                          Json2AMQValue( TheJSONValue.value() ) );

      return AMQValue;
    }    
  }

  // A dummy return to avoid the compiler error for a function not retruning 
  // a value. This point should never be reached.

  return proton::value();
}

// ----------------------------------------------------------------------------
// Converting to JSON values
// ----------------------------------------------------------------------------
//
// It is necessary to be able to decode also the values of a structured
// AMQ message to JSON values in order to correctly initialise this 
// JSON message.

JSON JSONMessage::AMQValue2Json( const proton::value & TheValue ) const
{
  switch ( TheValue.type() )
  {
    case proton::type_id::ARRAY:
    case proton::type_id::LIST:
    {
      // First we need to obtain the values of the received array in a 
      // standard vector to be converted individually to JSON objects.
      // Note that there is no JSON type for lists, and so an AMQ list
      // will be treated as a sequence of values and stored as a JSON 
      // array structure.

      std::vector< proton::value > ValueSequence;
      proton::get( TheValue, ValueSequence );

      // The result is the internal type used by the JSON library and
      // this is returned as the JSON array object

      JSON::array_t TheConvertedArray;

      for( const auto & Value : ValueSequence )
        TheConvertedArray.emplace_back( AMQValue2Json( Value ) );

      return TheConvertedArray;
    }
    case proton::type_id::BINARY:
      return static_cast< std::string >( 
             proton::get< proton::binary >( TheValue ) );
    case proton::type_id::BOOLEAN:
      return proton::get< bool >( TheValue );
    case proton::type_id::BYTE:
      return proton::get< int8_t  >( TheValue );
    case proton::type_id::CHAR:
      return proton::get< wchar_t >( TheValue );
    case proton::type_id::DECIMAL128:
    case proton::type_id::DECIMAL32:
    case proton::type_id::DECIMAL64:
    {
      // These types are encoded as byte arrays, and there must be some
      // internal format to map the sequence of bits to a decimal value. This
      // encoding is not documented, and so the most robust way to convert 
      // these types will be to use their stream output functions and then 
      // read the converted string back into a long double to preserve as
      // as much of the extended matissa as possible.

      std::stringstream ValueConverter;
      long double Result;

      ValueConverter << TheValue;
      ValueConverter >> Result;
      
      return Result;
    }
    case proton::type_id::DESCRIBED:
      // It is not clear what this is and the best is to return an empty value
      return JSON();
    case proton::type_id::DOUBLE:
      return proton::get< double >( TheValue );
    case proton::type_id::FLOAT:
      return proton::get< float >( TheValue );
    case proton::type_id::INT:
      return proton::get< int32_t >( TheValue );
    case proton::type_id::LONG:
      return proton::get< int64_t >( TheValue );
    case proton::type_id::MAP:
    {
      // The map is a sequence of attribute-value pairs that can be extracted
      // in the same way as the array and lists are extracted.

      std::map< std::string, proton::value > Values;
      proton::get( TheValue, Values );

      // The values are then mapped to JSON structures one by one keeping the
      // key name as the JSON attribute name

      JSON::object_t JsonMap;

      for( const auto & [AttributeName, Value] : Values )
        JsonMap.emplace( AttributeName, AMQValue2Json( Value ) );

      return JsonMap;
    }
    case proton::type_id::NULL_TYPE:
      return JSON();
    case proton::type_id::SHORT:
      return proton::get< int16_t >( TheValue );
    case proton::type_id::STRING:
      return proton::get< std::string >( TheValue );
    case proton::type_id::SYMBOL:
      // A symbol is just a string, That can be returned
      return proton::get< proton::symbol >( TheValue );
    case proton::type_id::TIMESTAMP:
      // The time stamp is the number of milliseconds since the epoch of the 
      // unix clock at 00:00:00 (UTC), 1 January 1970. 
      return proton::get< proton::timestamp >( TheValue ).milliseconds();
    case proton::type_id::UBYTE:
      return proton::get< uint8_t >( TheValue );
    case proton::type_id::UINT:
      return proton::get< uint32_t >( TheValue );
    case proton::type_id::ULONG:
      return proton::get< uint64_t >( TheValue );
    case proton::type_id::USHORT:
      return proton::get< uint16_t >( TheValue );
    case proton::type_id::UUID:
      // The user identifier can be converted into a string by a member
      // function to retrun it into a string JSON type
      return proton::get< proton::uuid >( TheValue ).str();
  }

  // Returning a dummy response as this point should never be reached but it 
  // avoids a cmpiler warning that the function is not returning as the return
  // depends on the switch cases above.

  return JSON();
}

/*=============================================================================

  Converting messages

=============================================================================*/
//
// Producing the AMQ messate from the JSON message is then just a matter of 
// first creating the message handler, and populating the message content 
// type and body.

JSONMessage::ProtocolPayload JSONMessage::GetPayload( void ) const
{
  ProtocolPayload ThePayload = AMQ::Message::NewMessageHandle();

  ThePayload->content_type( UniqueMessageIdentifier );
  ThePayload->body( Json2AMQValue( *this ) );

  return ThePayload;
}

// ----------------------------------------------------------------------------
// Receiving meassages
// ----------------------------------------------------------------------------
//
// The issue with JSON is that it can be sent over the network in various ways
// depending on the capabilities of the sender. In general, the AMQ extensions
// for Theron++ uses structured AMQ messages for communication among Actors. 
// However, for some messages it could be that the JSON struct is forwarded 
// as a string that must be parsed. In either case, it is not possible to tell
// only from the message content which JSON message it corresponds to, and 
// one must therefore rely strictly on the content type. 

bool JSONMessage::Initialize( const ProtocolPayload & ThePayload ) noexcept
{
  if( ThePayload->content_type() == UniqueMessageIdentifier )
  {
    proton::value TheContent = ThePayload->body();

    if( TheContent.type() == proton::type_id::STRING )
      this->JSON::operator=( 
            parse( proton::get< std::string >( TheContent ) ) );
    else
      this->JSON::operator=( AMQValue2Json( TheContent ) );

    return true;
  }
  else 
    return false;
}

Address JSONMessage::PresentationLayerAddress( void ) const
{ return Theron::Network::GetAddress( Theron::Network::Layer::Presentation ); }


} // End namespace