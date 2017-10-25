/*=============================================================================
Serialisation example

The aim of this example is to show how messages can be sent transparently over
the network, and how such messages can be serialised and de-serialised on the 
sender side and the receiver side respectively. Simple vanilla serialisation 
is used in order to make the file self contained and not depend on external 
libraries. Please see the presentation layer header for better ways to do the 
serialisation in larger projects.

A minimal version of the session layer is also given. The normal session layer 
will forward the messages to the network layer for transmission, but this 
version simply prints the serialised payload for outbound messages, and 
forwards serial payloads to the presentation layer. 
	
The mechanisms demonstrated are general, and the normal presentation layer 
implementation is used, hence this example can also be seen as a way to test 
and verify the presentation layer implementation.

Author and Copyright: Geir Horn, 2017
License: LGPL 3.0
=============================================================================*/

#include <sstream>										 // Various string stream operations
#include <random>					  					 // To roll a dice
#include <chrono>										   // To seed the random generators
#include <mutex>										   // To protect the random generator
#include <vector>											 // To hold the wise words

#include <boost/algorithm/string.hpp>  // To convert to upper-case 

#include "Actor.hpp"									 // The Actor framework
#include "StandardFallbackHandler.hpp" // Reporting wrongly sent messages
#include "NetworkEndPoint.hpp"         // The network endpoint
#include "SerialMessage.hpp"           // Serial message format
#include "LinkMessage.hpp"             // Message to be sent on the link
#include "NetworkLayer.hpp"            // The link layer protocol server
#include "PresentationLayer.hpp"	  	 // The presentation layer actor extension
#include "ConsolePrint.hpp"						 // Sequential output

/*=============================================================================

 Simple Session Layer

=============================================================================*/
//
// The simple session layer provides a message handler to receive outbound 
// messages in serialised format, and then prints the message. It should be 
// noted that the user would never define a session layer or a presentation 
// layer, and the latter's remote message is a protocol for transferring 
// messages between the two networking layers. 
//
// It is defined as a replacement for the one provided by Theron++ and since 
// the remote message defined by the presentation layer is a part of the 
// communication layer protocol between the presentation layer and the session
// layer, it is accessible only for the session layer of Theron++. Hence, the 
// session layer must be defined in the Theron name space, and provide a method
// for sending serialised messages as if they arrive from the network.

namespace Theron{

template< class ExternalMessage >
class SessionLayer : public Theron::Actor
{
public:
	
  void OutboundMessage( 
		   const PresentationLayer::RemoteMessage & TheMessage,
			 const Address From                     )
	{
		Theron::ConsolePrint LogMessage;
		
		LogMessage << "[ OUTBOUND: ] \"" << TheMessage.GetPayload() << "\" From: "
		           << TheMessage.GetSender().AsString() << std::endl;
							 
	}
	
	// A remote message is generated when a serial payload arrives from the 
	// network and it is forwarded to the presentation layer.
	
	void ForwardNetworkMessage( Address From, Address To, 
															const SerialMessage::Payload & ThePayload )
	{
		Send( PresentationLayer::RemoteMessage( From, To, ThePayload ), 
				  Network::GetAddress( Network::Layer::Presentation ) );
	}
	
	// The constructor simply registers this message handler.
	
	SessionLayer( const std::string name = std::string() ) 
	: Actor( name )
	{
		RegisterHandler( this, &SessionLayer::OutboundMessage );
	}
};
	
}

/*=============================================================================

 Network endpoint initialiser

=============================================================================*/
//
// The network endpoint encapsulates the three communication layers of every 
// node: The link layer, the session layer, and the presentation layer. A dummy
// link layer is defined for this test as there is no real physical transmission
// being done

class DummyLinkLayer
: virtual public Theron::Actor, 
  virtual public Theron::StandardFallbackHandler,
  public Theron::NetworkLayer< Theron::LinkMessage< std::string > >
{
	
	// In order for this to be instantiated it must provide some virtual functions
	// that do nothing 
	
protected:
	
	virtual void ResolveAddress( const ResolutionRequest & TheRequest, 
														   const Address TheSessionLayer ) override
  { }
  
  virtual void ActorRemoval( const RemoveActor & TheCommand, 
														 const Address TheSessionLayer ) override
  { }
  
  // The outbound message should take an external message as argument, and 
  // the external message type is the template argument for the network layer
  
  virtual void OutboundMessage( 
						   const Theron::LinkMessage< std::string > & TheMessage, 
							 const Address From ) override
	{ }
  
public:
	
	// The constructor ensures that the base classes are correctly constructed
	// using default arguments
	
	DummyLinkLayer( void )
	: Actor(), StandardFallbackHandler(),
	  Theron::NetworkLayer< Theron::LinkMessage< std::string > >()
	{ }
	
	// The destructor should be virtual because the base classes are virtual 
	// and because the class has virtual functions.
	
	virtual ~DummyLinkLayer( void )
	{ }
};

// In order to support freely user defined classes an initialiser class must 
// be defined and the network endpoint will call two methods of this class
// in order: first the function to create the application specific layer classes
// and then the function to bind the classes. The last function will be empty 
// if there is no particular binding actions to do.
// 
// This is defined as a class derived from the endpoint class

class TestNode 
: virtual public Theron::Actor,
  Theron::NetworkEndPoint
{
	
};

/*=============================================================================

 Worker

=============================================================================*/
//
// The worker actor defines some messages supporting supporting serialisation
// and sends in response to various request messages.

class Worker : virtual public Theron::Actor,
							 virtual public Theron::DeserializingActor
{
public:
	
  // ---------------------------------------------------------------------------
	// Rolling the dice
	// ---------------------------------------------------------------------------
	//
	// The dice is a random number between 1 and 6. The best is to use one, single
	// generator for all workers since this gives the longest random sequence. 
	// The dice is defined as a private object hiding the syntax of the standard 
	// random generators and distributions.
	
private:
		
	class SixDice : public std::uniform_int_distribution< unsigned short > 
	{
	private:

		// First the generator engine is defined as the standard Mersenne Twister 
		// since it is among the best random generators. It is declared as static 
		// to be shared among all dices.
		
		#if __x86_64__ || __ppc64__ || _WIN64
	    static std::mt19937_64 MersenneTwister;
	  #else
	    static std::mt19937 MersenneTwister;
	  #endif
		
    // Since this is shared among all dices, which are owned by the workers 
		// that are threads, Access to the generator must be protected by a 
		// mutex.
			
		static std::mutex GeneratorAccess;
		
		// Finally, the dice will use the function operator of the distribution to 
		// produce the desired random face.
			
		using std::uniform_int_distribution< unsigned short > ::operator();
		
	public:
		
		// As a short-hand the result type is inherited from the distribution
		
		using result_type = 
					typename std::uniform_int_distribution< unsigned short >::result_type;
		
		// The actual numbers are produced by rolling the dice. This means accessing
		// the generator, and therefore the mutex must be locked first.
					
		result_type Roll( void )
		{
			std::lock_guard< std::mutex >  Lock( GeneratorAccess );
			return this->operator()( MersenneTwister );
		}
		
		// For convenience there is also a function operator that simply calls the 
		// roll function
		
		result_type operator() ( void )
		{
			return Roll();
		}
		
		// The default constructor simply sets the range of the uniform distribution
		
		SixDice( void )
		: std::uniform_int_distribution< unsigned short >( 1, 6 )
		{ }
		
		// The worker has a Dice that will be rolled if random numbers are requested
	} Dice;

	// ---------------------------------------------------------------------------
	// Request messages
	// ---------------------------------------------------------------------------
	//
	// The first message is a simple request to have a word of wisdom from a peer
	// worker. Its serialised form is just a key word indicating what kind of 
	// message this is.
	
	class AskGuru : public Theron::SerialMessage
	{
	protected:
		
		virtual std::string Serialize( void ) const
		{
			return std::string("AskGuru");
		}
		
		// De-serialising means just to check if the payload equals the expected
		// class type.
		
		virtual bool Deserialize( const Theron::SerialMessage::Payload & Payload )
		{
			std::string Command( Payload );
			boost::to_upper( Command );
			
			if ( Command == "ASKGURU" )
				return true;
			else
				return false;
		}		
		
	public:
		
		// The message has a default constructor and a copy constructor
		
		AskGuru( void ) = default;
		AskGuru( const AskGuru & OtherMessage ) = default;
		
		// Since the class is polymorphic it is best practice to provide a virtual 
		// destructor
		
		virtual ~AskGuru( void )
		{ }
	};

	// The Ask Guru message was a trivial example of a message that carried no 
	// data. As an alternative to the wisdom of a guru, one may resort to chance 
	// and ask the worker to roll a dice a number of times.
	
	class RollDice : public Theron::SerialMessage
	{
	private:
				
		unsigned int NumberOfRolls;
		
		// Serialising the message means just writing the command and the possible 
		// option values.
		
	protected:
		
		virtual std::string Serialize( void ) const
		{
			std::ostringstream Message;
			
			Message << "RollDice " << NumberOfRolls;
			
			return Message.str();
		}
		
		// De-serialisation follows the same pattern as above, with the number of 
		// rolls read as the second element of the payload if the command is 
		// matching.
		
		virtual bool Deserialize( 
						const Theron::SerialMessage::Payload & Payload )
		{				
			std::istringstream Message( Payload );
			std::string 			 Command;
			
			Message >> Command;
			boost::to_upper( Command );
			
			if ( Command == "ROLLDICE" )
		  {
				Message >> NumberOfRolls;				
				return true;
			}
			else 
				return false;
		}
			
	public:
		
		// The requested number of rolls are provided by an interface function
		
		inline unsigned int GetNumber() const
		{
			return NumberOfRolls;
		}
		
		// The default constructor simply takes the number of rolls as input and 
		// stores this number. 
		
		inline RollDice( unsigned int Rolls = 0 )
		: Theron::SerialMessage(), 
		  NumberOfRolls( Rolls )
		{ }
		
		// A copy constructor is mandatory for all messages. It simply delegates 
		// the construction to the default constructor.
		
		inline RollDice( const RollDice & Other )
		: RollDice( Other.NumberOfRolls )
		{ }
		
		// And it has a virtual empty destructor since it is a polymorphic object
		
		virtual ~RollDice( void )
		{ }
	};

  // ---------------------------------------------------------------------------
	// Response messages
	// ---------------------------------------------------------------------------
	//
  // There are two types of responses, one to return a string as the word of 
	// wisdom, and another to return a vector of random numbers.
	
	class WordsOfWisdom : public Theron::SerialMessage,
											  public std::string
	{
	protected:
	
		// Writing the message as a string is trivial
		
		virtual std::string Serialize( void ) const
		{
			return std::string("WordsOfWisdom ") + *this;
		}
		
		// De-serialising the message requires first a test to verify that the 
		// message received is really a Words of Wisdom message, and then set the 
		// content string
		
		virtual bool Deserialize( const Theron::SerialMessage::Payload & Payload )
		{
			std::istringstream Message( Payload );
			std::string 		   Command;
			
			// The standard ws manipulator is used to remove the white space between
			// the command string and the actual message.
			
			Message >> Command >> std::ws;
			boost::to_upper( Command );
			
			if ( Command == "WORDSOFWISDOM" )
			{
				// The standard function to get the reminding part of the line is used
				// to copy the actual words of wisdom to this binary message.
				
				std::getline( Message, *this );
				return true;
			}
			else
				return false;
		}
		
	public:

		// The default constructor simply initialises the string.
		
		WordsOfWisdom( const std::string TheWisdom = std::string() )
		: Theron::SerialMessage(),
		  std::string( TheWisdom )
		{ }
		
		// The copy uses the default constructor for initialisation
		
		WordsOfWisdom( const WordsOfWisdom & OtherMessage )
		: WordsOfWisdom( static_cast< std::string >( OtherMessage ) )
		{ }
		
		// The virtual destructor is again just a place holder
		
		virtual ~WordsOfWisdom( void )
		{ }
	};
	
	// The second response type is a vector of random numbers obtained from 
	// rolling the dice a specific number of times. These numbers as stored in 
	// a standard vector.
	
	class Faces : public Theron::SerialMessage,
								public std::vector< SixDice::result_type >
	{
	protected:
		
		// Serialising this message is just giving the type of message and then 
		// all the values separated by spaces. Note that there is no need to send 
		// the length of the vector since the values are added as long as there 
		// are more values to read.
		
		virtual std::string Serialize( void ) const
		{
			std::ostringstream Message;
			
			Message << "Faces ";
			
			for ( unsigned short Number : *this )
				Message << Number << " ";
			
			return Message.str();
		}
		
		// Reconstructing the vector from a serial message is therefore just 
		// to verify the type of message and then read the values as long as there
		// are values available.
		
		virtual bool Deserialize( const Theron::SerialMessage::Payload & Payload )
		{
			std::istringstream Message( Payload );
			std::string   	   Command;
			
			Message >> Command >> std::ws;
			boost::to_upper( Command );
			
			if ( Command == "FACES" )
			{
				while ( ! Message.eof() )
				{
					unsigned short Number;
					Message >> Number >> std::ws;
					push_back( Number );
				}
				
				return true;
			}
			else
				return false;
		}
		
	public:
		
		// The default constructor takes a vector and copies the content to the 
		// message. 
		
		Faces( const std::vector< SixDice::result_type > & GivenValues = {} )
		: Theron::SerialMessage(),
		  std::vector< SixDice::result_type >( GivenValues )
		{ }
		
		// The copy constructor just uses the previous constructor
		
		Faces( const Faces & OtherMessage )
		: std::vector< SixDice::result_type >( OtherMessage )
		{ }
		
		// The virtual destructor is again empty
		
		virtual ~Faces( void )
		{ }
	};
	
  // ---------------------------------------------------------------------------
	// Message handlers
	// ---------------------------------------------------------------------------
	//
	// The worker must be able to receive and possibly respond to all the four 
	// message types above. The request messages are handled first, starting with
	// the message handler to produce a wisdom.
	
	void Guru( const AskGuru & Request, const Theron::Address Sender )
	{
		const static std::vector< std::string > TheWisdoms = {
			{"The answer is 42!"},
			{"Godot will soon be here!"},
			{"Vanity of vanity! Everything is vanity!"},
			{"What goes up, must come down!"},
			{"Panta rei!"},
			{"Welcome to the machine!"}
		};
		
		// The wisdom to return is chosen by rolling the dice, but since the dice 
		// has values in the set {1,...,6} it is necessary to subtract one before 
		// using it to look up a wisdom string.
		
		Send( WordsOfWisdom( TheWisdoms[ Dice()-1 ] ), Sender );
	}
	
	// Rolling the the dice is an equally simple request handler
	
	void Gambler( const RollDice & Request, const Theron::Address Sender )
	{
		std::vector< SixDice::result_type > Outcome( Request.GetNumber() );
		
		for ( SixDice::result_type & Play : Outcome )
			Play = Dice();
		
		Send( Faces( Outcome ), Sender );
	}
	
	// The response handlers simply writes out a string to the console print 
	// utility actor.
	
	void Interpreter( const WordsOfWisdom & Response, 
										const Theron::Address TheGuru )
	{
		Theron::ConsolePrint Revelation;
		
		Revelation << "The worker " << GetAddress().AsString() 
							 << " is contemplating the following wisdom: \""
							 << Response << "\" delivered by " 
							 << TheGuru.AsString() << std::endl;
							 
	}
	
	void Coupier( const Faces & TheFortune, const Theron::Address Sender )
	{
		Theron::ConsolePrint Outcome;
		
		Outcome << "The worker " << GetAddress().AsString() << " had the "
						<< "following fortune:  ";
						
		for ( auto Roll : TheFortune )
			Outcome << Roll << " ";
		
		Outcome << "received from " << Sender.AsString() << std::endl;
	}

  // ---------------------------------------------------------------------------
	// Utility functions
	// ---------------------------------------------------------------------------
	//
	// There are two utility functions: one used to request some wisdom from 
	// a peer worker, and one to ask another peer worker to roll the dice some 
	// given number of times.
	
public:
	
	inline void RequestWisdom( const Theron::Address & WisePeer )
	{
		Send( AskGuru(), WisePeer );
	}

	inline void RequestFortune( const Theron::Address & Casino, 
															short int NumberOfRolls )
	{
		Send( RollDice( NumberOfRolls ), Casino );
	}
	
	
  // ---------------------------------------------------------------------------
	// Constructor
	// ---------------------------------------------------------------------------
	//
	// The constructor function registers the above message handlers and then 
	// the worker is good to go.
	
public:
	
	Worker( const std::string name = std::string() )
	: Actor( name ), DeserializingActor( name ),
	  Dice()
	{
		RegisterHandler( this, &Worker::Guru        );
		RegisterHandler( this, &Worker::Gambler     );
		RegisterHandler( this, &Worker::Interpreter );
		RegisterHandler( this, &Worker::Coupier     );
	}
};


// it is necessary to define the static variables of the dice. The generator 
// is initialised with the current time.

#if __x86_64__ || __ppc64__ || _WIN64
  std::mt19937_64 Worker::SixDice::MersenneTwister( 
		std::chrono::system_clock::now().time_since_epoch().count()
	);
#else
  std::mt19937 Worker::SixDice::MersenneTwister(
		std::chrono::system_clock::now().time_since_epoch().count()
	);
#endif

std::mutex Worker::SixDice::GeneratorAccess;

/*=============================================================================

 Main

=============================================================================*/
//
// The main function sets up the presentation layer and the session layer and 
// a couple of workers. Then some messages are tested, first they are sent 
// as binary messages, and then they are sent as as serialised messages from 
// the session server to test the de-serialisation. The serialisation can be 
// simulated by sending a message to a non-existing actor address. 

int main(int argc, char **argv) 
{
	Theron::ConsolePrintServer           TheConsole( &std::cout, 
																									 "ConsolePrintServer");
	Theron::SessionLayer< std::string >  SessionServer( "SessionServer" );
	Theron::PresentationLayer            ThePresentationLayer;
	
	// Binding the sessions server to the presentation layer server
	
	ThePresentationLayer.SetSessionLayerAddress( SessionServer.GetAddress() );
	
	// In this toy example two workers are defined
	
	Worker FirstWorker(  "First_Worker"  ), 
				 SecondWorker( "Second_Worker" );
	
	// Then binary messages are tested. Each worker asks the other for a word 
	// of wisdom and as many rolls of the dices as there are letters in the
	// names of the workers. It should be noted that it is bad practice to 
	// explicitly send the messages on behalf of the workers as done here.
	
	FirstWorker.RequestWisdom ( SecondWorker.GetAddress() );
	SecondWorker.RequestWisdom( FirstWorker.GetAddress()  );
	
	FirstWorker.RequestFortune ( SecondWorker.GetAddress(), 
															 FirstWorker.GetAddress().AsString().length() );
	SecondWorker.RequestFortune( FirstWorker.GetAddress(), 
															 SecondWorker.GetAddress().AsString().length() );
	
	// To test the serialised messages, the similar requests will be made through 
	// the session layer, but indicating the first worker as sender. The second 
	// worker should then get the message as a binary message after 
	// de-serialisation, and respond with a message that will reach the first 
	// worker as a binary message since the presentation layer does not need to 
	// serialise the given message since the first worker's address is know as 
	// a local actor.
	
	SessionServer.ForwardNetworkMessage( FirstWorker.GetAddress(), 
																			 SecondWorker.GetAddress(), 
																			 "AskGuru" );
	
	SessionServer.ForwardNetworkMessage( FirstWorker.GetAddress(), 
																			 SecondWorker.GetAddress(), 
																			 "RollDice 5" );

	SessionServer.ForwardNetworkMessage( Theron::Address("Remote_Actor"), 
																			 FirstWorker.GetAddress(), 
																			 "Faces 1 2 3 4 5 6 " );
	
	// That was a test of the de-serialisation made at the worker actor
	// showing that the binary messages were correctly constructed from the 
	// string payload. Next is testing that the response will be correctly 
	// intercepted by the Presentation Layer when the address not known 
	// as an actor on this endpoint. The Presentation Layer will then serialise
	// the message and send it to the session layer, which here only prints 
	// the content of the serial payload. 
	//
	// This time the request for the words of wisdom goes to the first worker 
	// actor and  the request to roll the dice goes to the second worker actor.
	
	SessionServer.ForwardNetworkMessage( Theron::Address("Remote_Actor"), 
																			 FirstWorker.GetAddress(), 
																			 "AskGuru" );
	
	SessionServer.ForwardNetworkMessage( Theron::Address("Remote_Actor"), 
																			 SecondWorker.GetAddress(), 
																			 "RollDice 8" );

	// Then it is just to wait for all messages to be handled as the actor system 
	// can terminate when there are no more pending messages. This is necessary to 
	// ensure that main does not terminate and end the process and kills all 
	// actors even if some actors are not done with their message processing.	
	//
	// Beware! This technique only works on actor systems on a single network
	// endpoint. If there are remote actors on other endpoints they can still 
	// generate messages for actors on this endpoint, and no pending messages on 
	// this endpoint is therefore no guarantee that the actor sub-system on this 
	// endpoint can be closed. In distributed settings it is necessary to 
	// implement an application level protocol between the endpoints to ensure 
	// that all actors on all endpoints have finished processing.
	
	Theron::Actor::WaitForGlobalTermination();
	
	// Everything should be OK, and so it is just to exit happily.
	
	return EXIT_SUCCESS;
}
