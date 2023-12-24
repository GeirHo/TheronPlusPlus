/*==============================================================================
Active Message Queue (AMQ) Unit Test

This test file creates two Actors using the AMQ communication infrastructure to 
exchange messages. One of the actors is a Hello world example that can be
created as a sender or responder. The sender wil send the "Hello World!"
message to the responder actor whose global address has to be resolved. The 
responder will reply with a multi-part message to illustrate how more complex
message formats can be exchange using the AMQ proptocol.

The second Actor will subscribe to a given topic and print the messages 
received where the format is that the message contains a JavaScript Object 
Notation (JSON) meessage with separate fields fot the attribute and the value. 
The JSON implementation uses the best JSON library available [1].

The command line options for this unit test are the following. First the 
responder should be started

./AMQDistribution -I HelloResponder --endpoint Responder \
 --broker localhost --port 5672 --user admin --password admin \
 --topics TestTopic

Then one can start the sender

./AMQDistribution -I HelloSender -R HelloResponder --endpoint Sender \
--broker localhost --port 5672 --user admin --password admin \
--topics TestTopic

Provided that the AMQ broker is accessible through the localhost URL on the 
AMQ Protocol port 5672 with standard user and password. Note that the enpoints
must be different for the server to recognise this as two different clients 
connecting.

The command line options are parsed by the cxxopt parser [2]. This was chosen
becuase its syntax is similar to Boost Program Options, but it uses standard
C++ libraries and does not require linking against external libraries making 
the code more portable.

References: 
[1] https://github.com/nlohmann/json
[2] https://github.com/jarro2783/cxxopts

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
==============================================================================*/

// Standard headers

#include <string>           // For standard strings
#include <memory>           // For smart pointers
#include <source_location>  // Making informative error messages
#include <sstream>          // To format error messages
#include <stdexcept>        // standard exceptions
#include <initializer_list> // To unpack variable arguments
#include <concepts>         // To constrain types
#include <vector>           // To store subscribed topics
#include <thread>           // To sleep while waiting for termination
#include <chrono>           // To have a concept of fime

// Theron++ headers

#include "Actor.hpp"
#include "Utility/StandardFallbackHandler.hpp"
#include "Utility/ConsolePrint.hpp"

#include "Communication/PolymorphicMessage.hpp"
#include "Communication/NetworkingActor.hpp"

// AMQ related headers

#include "proton/message.hpp"
#include "Communication/AMQ/AMQMessage.hpp"
#include "Communication/AMQ/AMQEndpoint.hpp"
#include "Communication/AMQ/AMQjson.hpp"

// The cxxopts command line options parser

#include "cxxopts.hpp"

/*==============================================================================

 Hello World Actor

==============================================================================*/
//
// The Hello World actor can be instantiated as the sender or as the responder
// using two different messages. 

class HelloWorld
: virtual public Theron::Actor,
  virtual public Theron::StandardFallbackHandler,
  virtual public Theron::NetworkingActor< 
    typename Theron::AMQ::Message::PayloadType >
{
  // --------------------------------------------------------------------------
  // Request message
  // --------------------------------------------------------------------------
  //
  // The request message is basically a string, and it is forwarded as 
  // string payload of the AMQ message

public:

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

    std::string_view MessageID{ "HelloWorld::RequestMessage" };

    // The message is contained in a standard string which is initialised when
    // the message is constructed.

    std::string TheMessageText;

    // There is only one presentation layer in this demonstration application
    // and so the global address function can be used to provide the address
    // of the Presentation Layer server

  protected:

    virtual Address PresentationLayerAddress( void ) const override
    { return Theron::Network::GetAddress( 
             Theron::Network::Layer::Presentation ); }

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
  // The response is encoded as a JSON object as an attribute-value pair. where
  // the attributes are all string labels. The JSON object can be serialised to
  // a string. However, the JSON message defined for Theron++ will try to use 
  // the structured supported by AMQ messages hopefully allowing for binary and
  // structured transmission of messages if the Network Layer finds that it is
  // possible between two endpoints. In both cases, the binary message at the 
  // receiver will construct a JSON object whose structure reflects the one 
  // received. This may not be the same as the one the message format should 
  // have, and therefore the a uniqe message identification string must be 
  // provided with the message allowing differnet JSON object messages on the 
  // same Actor to detect if the message should be decoded or not.

  class ResponseMessage 
  : virtual public Theron::AMQ::JSONMessage
  {
    protected:

      virtual Address PresentationLayerAddress( void ) const override
      { return Theron::Network::GetAddress( 
               Theron::Network::Layer::Presentation ); }

      // The Get Payload function and the function to initialise the message
      // from a payload are already implemented by the JSON message base class
      // and thus they need not be defined here.
      //
      // The constructors are either default or taking the initaliser for the 
      // JSON message

    public:

      ResponseMessage( void )
      : JSONMessage( "HelloWorld::ResponseMessage" )
      {}

      ResponseMessage( const JSON::object_t & JSONData )
      : JSONMessage( "HelloWorld::ResponseMessage", JSONData )
      {}

      virtual ~ResponseMessage() = default;
  };

  // --------------------------------------------------------------------------
  // Message handlers
  // --------------------------------------------------------------------------
  //
  // The handler for the request message simply return to the sender a simple
  // structured JSON structured message. The response is here hardcoded to 
  // for the simplicity of the test. This message can of course contain many 
  // different attributes and values.

private:

  void HandleRequest( const RequestMessage & TheRequest, 
                      const Address RequestingActor )
  {
    Theron::ConsoleOutput Output;

    Output << "HANDLING REQUEST " << std::endl;
    
    ResponseMessage TheResponse({
                    {"Message received", TheRequest.Text() }, 
                    {"Decision", "Idiot"}, 
                    {"Reply", "Life is just the time it takes to die!"},
                    {"Answer", 42u} });

    Send( TheResponse, RequestingActor );
  }

  // The handler receiving the response will simply print the response to the 
  // standard output. However, since actros are running in threads one cannot 
  // directly write to the console, but rather send the output as messages
  // to an actor responsible for sequencing the formatted strings to the 
  // console.

  // The message handler can then use this to print the keys and the content
  // of the received JSON object.

  void HandleResponse( const ResponseMessage & TheResponse, 
                       const Address RespondingActor )
  {
    Theron::ConsoleOutput Output;

    Output << "HANDLING RESPONSE " << std::endl;
    if( TheResponse.empty() )
      Output << "Got an empty JSON response..." << std::endl;
    else if( TheResponse.is_primitive() )
      Output << "Got a primitive JSON value : " << TheResponse << std::endl;
    else
      Output << "Received the following response: "  << std::endl
             << TheResponse.dump(2) << std::endl;
  }

  // --------------------------------------------------------------------------
  // Constructor and destructor
  // --------------------------------------------------------------------------
  //
  // The constructor requires the name of the acto and the address of the actor 
  // to respond to the Hello World  request. If no responder actor address is 
  // given, then the instance of this actor is taken to be the passive responder 
  // actor.
  //
  // The constructor first registers the two message handlers, and then
  // sends the hello world message. One may be tempted to create a method on 
  // the actor to trigger the hello world message, but that would violate the 
  // principle that an Actor is only supposed to react to messages. 

public:

  HelloWorld( const std::string & ActorName, 
              const Address HelloDestination = Address() )
  : Actor( ActorName ), 
    StandardFallbackHandler( Actor::GetAddress().AsString() ),
    NetworkingActor( Actor::GetAddress().AsString(), 
        Theron::Network::GetAddress( Theron::Network::Layer::Session ) )
  {
    RegisterHandler( this, &HelloWorld::HandleRequest  );
    RegisterHandler( this, &HelloWorld::HandleResponse );

    if( HelloDestination != Address() )
    {
      RequestMessage TheRequest("Hello to the big world!");
      Send( TheRequest, HelloDestination );
    }
  }

  HelloWorld() = delete;
  virtual ~HelloWorld() = default;
};

/*==============================================================================

 Topic subscriber

==============================================================================*/
//
// The purpose of this actor is to subscribe to one or more topics and simply 
// print the messages received from the subscribed topics. It is assumed that 
// all the topics pass the same message type, here a JSON message. However,
// there is a complication with topics as we will see shortly.

class TopicSubscriber
: virtual public Theron::Actor,
  virtual public Theron::StandardFallbackHandler,
  virtual public Theron::NetworkingActor< 
    typename Theron::AMQ::Message::PayloadType >
{
  // First redefine the payload type expected by the Networking Actor. Note 
  // that this is identical to the payload type defined by the AMQ message for
  // this demonstration.

protected:
  
  using ProtocolPayload = Theron::PolymorphicMessage< 
          typename Theron::AMQ::Message::PayloadType >::PayloadType;


  // --------------------------------------------------------------------------
  // Topic message
  // --------------------------------------------------------------------------
  //
  // The message on a topic can come from many differebt senders, and it need
  // not be encoded according to the JSON message expectation having a unique 
  // message identifier as the content type of the message. On general, one 
  // must define one message type per topic.
  //
  // Furthermore, to construct the JSON object from the message, the content 
  // type of thr message must match the defined unique message identifier, but
  // the content type string is probably not set for the message sent on the 
  // topic, or the message type can be identical for many topics. If the sender
  // has not encoded the message identifier in the 'content type' string, then
  // the AMQ session layer will set it equal to the topic name. This allows 
  // different message classes to be defined for each topic, and still do 
  // indiviudal message handling.
  //
  // However, in this case we would like the ability to subscribe to various 
  // topics sending JSON messages with one single class. The trick is to 
  // override the decoding function to set the message 'content type' equal
  // to the topic message class identifier string. This will then make the 
  // generic class identifier always match for the AMQ JSON message decoding.

public:

  class TopicMessage
  : virtual public Theron::AMQ::JSONMessage
  {
  protected:

    virtual bool 
    Initialize( const ProtocolPayload & ThePayload ) noexcept override
    {
      ThePayload->content_type( GetMessageIdentifier() );
      return JSONMessage::Initialize( ThePayload );
    }

  public:

    TopicMessage( const std::string & MessageIdentifier )
    : JSONMessage( MessageIdentifier )
    {}

    TopicMessage()
    : JSONMessage( "TopicSubscriber::TopicMessage" )
    {}

    TopicMessage( const TopicMessage & Other )
    : JSONMessage( Other.GetMessageIdentifier(), Other )
    {}

    virtual ~TopicMessage() = default;
  };

  // --------------------------------------------------------------------------
  // Message handler
  // --------------------------------------------------------------------------
  //
  // The topic message will just be printed out to the console with the address
  // of the sender as this is the topic name. Binding the output to the console
  // writer is again necessary to avoid interleaving characters from multiple 
  // printouts.

private:

  void PrintTopicMessage( const TopicMessage & TheMessage, 
                          const Address TopicName )
  {
    Theron::ConsoleOutput Output;

    Output << "To Actor "<< GetAddress().AsString() << " from topic " 
           << TopicName.AsString() <<": " << std::endl
           << TheMessage.dump(2) << std::endl;
  }

  // --------------------------------------------------------------------------
  // Constructor and destructor
  // --------------------------------------------------------------------------
  //
  // The constructor takes the name of the actor as input and a sequence of 
  // topic names to be subscribed to. It will make the subscriptions for these
  // topics with the session layer, and then wait to receive JSON messages on
  // the subscribed topics. The topics are cached so that the actor can 
  // unsubscribe when the actor closes.

  std::vector< Theron::AMQ::TopicName > SubscribedTopics;

  // The first constructor is based on receiving a vector of topic names 
  // readily formatted. It registers the handler and then subscribes to the
  // given topics one by one.

public:
  
  TopicSubscriber( const std::string & ActorName, 
                   const std::vector< Theron::AMQ::TopicName > & TheTopics )
  : Actor( ActorName ),
    StandardFallbackHandler( Actor::GetAddress().AsString() ),
    NetworkingActor( Actor::GetAddress().AsString(), 
        Theron::Network::GetAddress( Theron::Network::Layer::Session ) ),
    SubscribedTopics( TheTopics )
  {
    RegisterHandler( this, &TopicSubscriber::PrintTopicMessage );

    for( const Theron::AMQ::TopicName & ATopic : SubscribedTopics )
    {
      Theron::AMQ::SessionLayer::TopicSubscription ASubscription( 
        Theron::AMQ::SessionLayer::TopicSubscription::Action::Subscription, 
        ATopic );

      Send( ASubscription, 
            Theron::Network::GetAddress( Theron::Network::Layer::Session ) );
    }
  }

  // The topics can also be given directly as various string types directly 
  // to the constructor, and this just use the given arguments and delegates 
  // to the previous vector based constructor unpacking the given strings as 
  // an initialiser list for the vector.

  TopicSubscriber( const std::string & ActorName,
                   std::convertible_to< Theron::AMQ::TopicName > 
                      auto && ...TheTopics )
  : TopicSubscriber( ActorName, {TheTopics...} )
  {}

  // The default constructor and the copy construxtor are simply deleted

  TopicSubscriber() = delete;
  TopicSubscriber( const TopicSubscriber & Other ) = delete;

  // The destructor unsubscribes from all the topics to ensure that the Session
  // Layer server deletes the subscriptions if this actor was the only actor 
  // subscribing to a topic.

  virtual ~TopicSubscriber()
  {
    for( const Theron::AMQ::TopicName & ATopic : SubscribedTopics )
    {
      Theron::AMQ::SessionLayer::TopicSubscription CancelSubscription( 
      Theron::AMQ::SessionLayer::TopicSubscription::Action::CloseSubscription, 
      ATopic );

      Send( CancelSubscription, 
            Theron::Network::GetAddress( Theron::Network::Layer::Session ) );
    }
  }
}; 

/*==============================================================================

 Main

==============================================================================*/
//
// The first step is to define the command line options supported and parse the
// command line arguments given. Then the Console Print Actor is constructed 
// first since it is necessary for the other actors to produce any output, then 
// the AMQ Endpoint is constrcuted so that the servers for the network are 
// running when the actors producing and consuming messages will be up running.

int main( int NumberOfCLIOptions, char ** CLIOptionStrings )
{
  // --------------------------------------------------------------------------
  // Defining and parsing options
  // --------------------------------------------------------------------------

  cxxopts::Options CLIOptions("./AMQDistribution",
    "Distributed Actors communicating using the AMQ message protocol");

  CLIOptions.add_options()
    ("R,responder", "Name of the remote Hello World responder", 
        cxxopts::value<std::string>()->default_value("") )
    ("I,InteractionActorName", "Name of the hello world actor", 
        cxxopts::value<std::string>()->default_value("HelloWorldActor"))
    ("E,endpoint", "The endpoint name", cxxopts::value<std::string>() )
    ("B,broker", "The URL of the AMQ broker", 
        cxxopts::value<std::string>() )
    ("P,port", "TCP port on  AMQ Broker", 
        cxxopts::value<unsigned int>() )
    ("U,user", "The user name used for the AMQ Broker connection", 
        cxxopts::value<std::string>() )
    ("Pw,password", "The password for the AMQ Broker connection", 
        cxxopts::value<std::string>() )
    ("t,topics", "Last option followed by a sequence of topic names",
        cxxopts::value< std::vector< Theron::AMQ::TopicName > >() )
    ("h,help", "Print help on use");

  CLIOptions.allow_unrecognised_options();
  CLIOptions.parse_positional({"topics"});

  auto CLIValues = CLIOptions.parse( NumberOfCLIOptions, CLIOptionStrings );

  if( CLIValues.count("help") )
  {
    std::cout << CLIOptions.help() << std::endl;
    exit( 0 );
  }

  // --------------------------------------------------------------------------
  // Constructing the Actors
  // --------------------------------------------------------------------------
  //
  // The network endpoint takes the endpoint name as the first argument, then 
  // the URL for the broker and the port number. The user name and the password
  // are defined in the AMQ Qpid Proton connection options, and the values are
  // therefore set for the connection options if they are given. Note that 
  // if the user name is given, then the password must also be given.

  proton::connection_options AMQOptions;

  if( CLIValues.count("user") && CLIValues.count("password") )
  {
    AMQOptions.user( CLIValues["user"].as< std::string >() );
    AMQOptions.password( CLIValues["password"].as< std::string >() );
  }

  // Then the network endpoint cna be constructed using the default names for
  // the various network endpoint servers in order to pass the defined 
  // connection options.

  Theron::AMQ::NetworkEndpoint AMQNetWork( 
    CLIValues["endpoint"].as< std::string >(), 
    CLIValues["broker"].as< std::string >(),
    CLIValues["port"].as< unsigned int >(),
    Theron::AMQ::Network::NetworkLayerLabel,
    Theron::AMQ::Network::SessionLayerLabel,
    Theron::AMQ::Network::PresentationLayerLabel,
    AMQOptions
  );

  // The topic subscriber is started on the topic names given on the command
  // line interface. 

  TopicSubscriber TopicServer( "TopicSubscriber", 
           CLIValues["topics"].as< std::vector< Theron::AMQ::TopicName > >() );

  // The Hello World actor will be started with the responder Actor's address.
  // If this is not given, the address will be empty and the actor will act as
  // a responder waiting for the incoming Hello Word message.

  std::string InteractionActorName 
              = CLIValues["InteractionActorName"].as<std::string>();

  Theron::Address HelloWorldResponder;

  if( CLIValues.count("responder") ) 
    HelloWorldResponder = 
        Theron::Address( CLIValues["responder"].as< std::string >()  );

  HelloWorld 
  InteractionActor( CLIValues["InteractionActorName"].as< std::string >(), 
                    HelloWorldResponder );

  // --------------------------------------------------------------------------
  // Waiting and closing
  // --------------------------------------------------------------------------
  //
  // Termination is not obvious when one subscribes to external topics as it 
  // is no way to decdede when the very last message has been sent on a topic.
  // Thus the way it is done here is just to wait for 5 minutes, then ask the 
  // network endpoint to close.

  std::this_thread::sleep_for( std::chrono::minutes(5) );

  // A stop message is sent to the network endpoint using the static function
  // to send the message, and the GetAddress is a function that can be safely
  // called on an actor since it is constant and cannot change the actor's 
  // state and can therefore be used from any thread.

  Theron::Actor::Send( Theron::Network::ShutDown(), 
                       Theron::Actor::Address(), AMQNetWork.GetAddress() );

std::cout << "Ready for closing" << std::endl;
  // Then it is just to wait for all the local actors to finish handling 
  // messages. The topic susbscriber will drain as soon as the network stops
  // receiving messages.

  Theron::Actor::WaitForGlobalTermination();
	
	return EXIT_SUCCESS;
}
