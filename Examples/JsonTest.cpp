/*==============================================================================
JSON AMQ message encoding and decoding test

This small test programme is made to validate the conversion of JSON messages 
using the converstion to an AMQ Proton message and then recovering the JSON 
from that message. It is illustrating the operations done by the presentation 
layer to convert the message to the AMQ Proton message when the message goes 
out to a remote actor, and the actions received by the Networking Actor message 
hander when an encoded JSON mwessage is received.'

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
==============================================================================*/


#include <iostream>
#include <numbers>
#include <limits>

#include "proton/message.hpp"

#include "Actor.hpp"
#include "Communication/PolymorphicMessage.hpp"
#include "Communication/AMQ/AMQjson.hpp"

/*==============================================================================

 Messages

==============================================================================*/
//
// The first message tested is a simple polymorphic message not using the JSON
// format just to test the basic principle of conversion to an AMQ Proton 
// message. 

class RequestMessage 
: public Theron::PolymorphicMessage< 
        typename Theron::AMQ::Message::PayloadType >
{

public:

using ProtocolPayload = PolymorphicMessage< 
        typename Theron::AMQ::Message::PayloadType>::PayloadType;

// It is recommended that one defines a unique string per message to be 
// encoded and decoded for network transmission. This string will be sent 
// as the content_type of the AMQ message and used to decide if the message 
// can be initialised from the arrived message or not.

private:

std::string_view MessageID{ "RequestMessage" };

// The message is contained in a standard string which is initialised when
// the message is constructed.

std::string TheMessageText;

// There is only one presentation layer in this demonstration application
// and so the global address function can be used to provide the address
// of the Presentation Layer server

public:

virtual Theron::Address PresentationLayerAddress( void ) const override
{ return Theron::Address(); }

// Obtaining the external AMQ payload from the message means to allocate 
// the Qpid Proton AMQ message and set the payload string. It also sets the 
// content_type string in the header of the AMQ message to match the name 
// of this class to facilitate decoding of the message at the receiver.

virtual PayloadType GetPayload( void ) const
{
    PayloadType TheMessage = std::make_shared< proton::message >();

    TheMessage->content_type( MessageID.data() );
    TheMessage->body( TheMessageText  );

    return TheMessage;
}

// The inverse function is initialising the message text with the string 
// value form the message payload. This it is a requirement that the 
// message can be default constructed and initialised after construction. 

virtual bool 
Initialize(  const PayloadType & TheMessage ) noexcept override
{
    if( TheMessage->content_type() == MessageID )
    {
    TheMessageText = proton::get<std::string>( TheMessage->body() );
    return true;
    }
    else
    return false;
}

// Since the message text string is private, the request message class
// can explicitly be converted to a string, or the text can be accessed
// by an interface function.

public:

inline std::string Text( void ) const
{ return TheMessageText; }

inline operator std::string () const
{ return TheMessageText; }

// The constructors are rather trivial. It will store the message text if
// it is given, otherwise the default actions are taken.

RequestMessage( const std::string & GivenMessageText )
: TheMessageText( GivenMessageText )
{}

RequestMessage( const RequestMessage & Other )
: RequestMessage( Other.TheMessageText )
{}

RequestMessage() = default;

// The destructor is virtual to properly destruct the inherited polymorphic
// message.

virtual ~RequestMessage() = default;
};

// --------------------------------------------------------------------------
// Response message
// --------------------------------------------------------------------------
//

class ResponseMessage 
: virtual public Theron::AMQ::JSONMessage
{
protected:

    virtual Theron::Address PresentationLayerAddress( void ) const override
    { return Theron::Address(); }

    // The Get Payload function and the function to initialise the message
    // from a payload are already implemented by the JSON message base class
    // and thus they need not be defined here.
    //
    // The constructors are either default or taking the initaliser for the 
    // JSON message

public:

    using Theron::AMQ::JSONMessage::GetPayload;
    using Theron::AMQ::JSONMessage::Initialize;

    ResponseMessage( void )
    : JSONMessage( "ResponseMessage" )
    {}

    ResponseMessage( const JSON & JSONData )
    : JSONMessage( "ResponseMessage", JSONData )
    {}

    virtual ~ResponseMessage() = default;
};

/*==============================================================================

 Main

==============================================================================*/
//

int main( int NumberOfCLIOptions, char ** CLIOptionStrings )
{

  RequestMessage TestMessage( "This is a test text!"), ReturnTest;

  RequestMessage::PayloadType ThePayload( TestMessage.GetPayload() );

  std::cout << "The message is of type " << ThePayload->content_type()
              << " With body: " << ThePayload->body() << std::endl;

  ReturnTest.Initialize( ThePayload );

  std::cout << "The message text recovered from the payload is "
              << ReturnTest. Text() << std::endl;

  ResponseMessage StructuredTest({   
                    {"Message received", ReturnTest.Text() }, 
                    {"Decision", "Idiot"}, 
                    {"Response", std::numbers::pi_v<long double> },
                    {"Reply", "Life is just the time it takes to die!"},
                    {"Answer", 42u} } ), 
                  RecoveredStructure;

  std::cout << "The initial JSON message is given as " << std::endl
            << StructuredTest.dump(2) << std::endl;

  ThePayload = StructuredTest.GetPayload();
  RecoveredStructure.Initialize( ThePayload );

  std::cout << "The recovered structrue after the payload " << std::endl
            << std::setprecision(std::numeric_limits<long double>::digits10 + 1)
            << RecoveredStructure.dump(2) << std::endl;

  return EXIT_SUCCESS; 
}