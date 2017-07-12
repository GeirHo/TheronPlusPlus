/*=============================================================================
  Serial Message
  
  Theron supports a message to be any class with no particular requirement on 
  the data it contains. However, if the message is to be sent to an actor on a 
  remote network endpoint, then the message must be encoded as a string to be 
  transmitted, and then decoded into a binary message at the receiving side. 
  
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
  performance, or the more elegant library for JSON [9], and then use JSON 
  messages among the actors. It is strongly recommended to implement the 
  serialising message handler using one of these libraries and not to implement 
  the serialisation mechanism in a non-standard way.
  
  All messages that may need to be sent across the network must be derived from 
  the Serial Message class defined here. It basically only defines two virtual 
  functions for the serialisation and the de-serialisation in order for the 
  Presentation Layer to take care of the correct network management. The class
  should therefore have been a nested type of the presentation layer. However, 
	the actor implementation offers an interface to the serialisation function, 
	and therefore the serial message class must be know when defining the actor. 
	Since the Presentation Layer is an actor, it cannot be defined before the 
	actor, and C++ does not support forward declaration of nested classes for some 
	incomprehensible reason. The only way is to declare the serial message as a 
	separate top-level object, which is done here.
  
  REFERENCES:
  
  [1] http://www.ocoudert.com/blog/2011/07/09/a-practical-guide-to-c-serialization/
  [2] http://www.boost.org/doc/libs/1_55_0/libs/serialization/doc/index.html
  [3] http://stackoverflow.com/questions/8115220/how-can-boostserialization-be-used-with-stdshared-ptr-from-c11
  [4] https://developers.google.com/protocol-buffers/
  [5] https://github.com/niXman/yas
  [6] http://uscilab.github.io/cereal/index.html
  [7] http://www.json.org/
  [8] https://github.com/open-source-parsers/jsoncpp
  [9] https://github.com/nlohmann/json
 
  Author: Geir Horn, University of Oslo, 2015 - 2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef THERON_SERIAL_MESSAGE
#define THERON_SERIAL_MESSAGE

namespace Theron
{

class SerialMessage
{
public: 
  
  // First we indicate to the compiler that this message can be serialised
  
	using IsSerial = std::true_type;
	
	// The payload is defined as a simple string
	
	using Payload = std::string;
  
  // Then we can define the functions to deal with serialisation.
	
protected:
	
  virtual std::string Serialize( void ) const = 0;
  virtual bool        Deserialize( const Payload & TheMessage ) = 0; 
	
	// These functions should be callable from the presentation layer and from
	// actors that needs to de-serialise incoming messages.
	
	friend class PresentationLayer;
	friend class DeserializingActor;
	
	// Messages that can serialised must provide a default constructor as
	// as the de-serialisation function will be used to initialise the 
	// message after construction.
	
public:
	
	SerialMessage( void ) = default;
};

}					// name space Theron
#endif  	// THERON_SERIAL_MESSAGE
