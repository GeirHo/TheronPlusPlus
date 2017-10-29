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
#include "DeserializingActor.hpp"      // To de-serialise messages
#include "LinkMessage.hpp"             // Message to be sent on the link
#include "NetworkLayer.hpp"            // The link layer protocol server
#include "PresentationLayer.hpp"	  	 // The presentation layer actor extension
#include "ConsolePrint.hpp"						 // Sequential output

/*=============================================================================

 Outside message

=============================================================================*/
//
// The network layer and the session layers are exchanging messages based on 
// the link layer message format. In this test case it is simply a class 
// holding a string for the sender address and a string for the receiver address
// and the payload of the message. It must be derived from the link message
// whose template parameter is the external address class, which is here a 
// a string.

class OutsideMessage : public Theron::LinkMessage< std::string >
{
public:
	
	using ExternalAddress = std::string;
	
private:
	
	ExternalAddress Sender, Receiver;
	Theron::SerialMessage::Payload ThePayload;
	
public:
	
	// Getting and setting the payload
	
  virtual Theron::SerialMessage::Payload GetPayload( void ) const override
  { return ThePayload; }
  
  virtual 
  void SetPayload( const Theron::SerialMessage::Payload & Payload ) override
  { ThePayload = Payload; }
  
  // Getting and setting the addresses
  
  virtual ExternalAddress GetSender   ( void ) const override
  { return Sender; }
  
  virtual ExternalAddress GetRecipient( void ) const override
  { return Receiver;	}
  
  virtual void SetSender   ( const ExternalAddress & From ) override
  { Sender = From; }
  
  virtual void SetRecipient( const ExternalAddress & To   ) override
  { Receiver = To; };

  // The message must provide a way to convert an external address to an 
  // address and in this case it is simple since the external address is
  // a string

  virtual Theron::Address 
  ActorAddress( const ExternalAddress & ExternalActor ) const override
  { 
    return Theron::Address( ExternalActor );
  }

  // The constructor simply initialises the fields in the same order as the 
  // initialisation operator of the link message
  
  OutsideMessage( const Theron::SerialMessage::Payload & GivenPayload, 
									const ExternalAddress & From, const ExternalAddress & To )
	: Theron::LinkMessage< ExternalAddress >(),
	  Sender( From ), Receiver( To ), ThePayload( GivenPayload )
	{	}

	virtual ~OutsideMessage( void )
	{ }
};

/*=============================================================================

 Network endpoint 

=============================================================================*/
//
// The network endpoint encapsulates the three communication layers of every 
// node: The Network layer, the session layer, and the presentation layer. A 
// dummy network layer is defined for this test as there is no real physical 
// transmission being done

class DummyNetworkLayer
: virtual public Theron::Actor, 
  virtual public Theron::StandardFallbackHandler,
  public Theron::NetworkLayer< OutsideMessage >
{	
protected:

	// Since the external address of an actor is simply a string, it can be 
	// set to the string representation of the actor ID, which is readily 
	// returned. 
	
	virtual void ResolveAddress( const ResolutionRequest & TheRequest, 
														   const Address TheSessionLayer ) override
  {
		Send( ResolutionResponse( TheRequest.NewActor.AsString(), 
															TheRequest.NewActor ), TheSessionLayer );
	}
  
  // The request for removing an actor will have no effect as there are no 
  // remote communication partners to inform about this event and no local 
  // actions to implement.
  
  virtual void ActorRemoval( const RemoveActor & TheCommand, 
														 const Address TheSessionLayer ) override
  { }
  
  // The outbound message should take an external message as argument, and 
  // the external message type is the template argument for the network layer.
  // In this simple test the content is simply printed.
  
  virtual void OutboundMessage( const OutsideMessage & TheMessage, 
																const Address From ) override
	{
		Theron::ConsolePrint LogMessage;
		
		LogMessage << "[ OUTBOUND: ] \"" << TheMessage.GetPayload() 
							 << "\" From: " << TheMessage.GetSender() << std::endl;
	}
  
public:

	// A remote message is generated when a serial payload arrives from the 
	// network and it is forwarded to the presentation layer.
	
	void ForwardNetworkMessage( const Address & From, const Address & To, 
											        const Theron::SerialMessage::Payload & 
											        ThePayload )
	{
		Send( OutsideMessage( ThePayload, From.AsString(), To.AsString() ), 
				  Theron::Network::GetAddress( Theron::Network::Layer::Session ) );
	}

	// The constructor ensures that the base classes are correctly constructed
	// using default arguments
	
	DummyNetworkLayer( void )
	: Actor( "NetworkLayerServer" ), 
	  StandardFallbackHandler( GetAddress().AsString() ),
	  Theron::NetworkLayer< OutsideMessage >( GetAddress().AsString() )
	{ }
	
	// The destructor should be virtual because the base classes are virtual 
	// and because the class has virtual functions.
	
	virtual ~DummyNetworkLayer( void )
	{ }
};

// In order to support freely user defined classes an initialiser class must 
// be defined and the network endpoint will call two methods of this class
// in order: first the function to create the application specific layer classes
// and then the function to bind the classes. The last function will be empty 
// if there is no particular binding actions to do.
// 
// This is defined as a class derived from the network class.

class SerialisationTest 
: virtual public Theron::Actor,
  public Theron::Network
{
protected:
	
	// There are three methods to create the layer servers. The standard session
	// and network layers can readily be reused for this test network.
	
	virtual void CreateNetworkLayer( void ) override
	{
		CreateServer< Layer::Network, DummyNetworkLayer >();
	}
	
	virtual void CreateSessionLayer( void ) override 
	{
	  CreateServer< Layer::Session, Theron::SessionLayer< OutsideMessage > >();
	}
	
	virtual void CreatePresentationLayer( void ) override
	{
		CreateServer< Layer::Presentation, Theron::PresentationLayer >();
	}
	
	// Then there is a handler for a message to tell the network to shut down 
	// when all other actors are done with their work. Here it just prints a 
	// message.
	
	virtual void StartShutDown( const Network::ShutDownMessage & TheMessage, 
													    const Address Sender ) override
	{
		Theron::ConsolePrint LogMessage;
		LogMessage << "Shut down message received... " << std::endl;
	}

	// The constructor needs to take the location of this node and pass it on 
	// as the domain to the generic base class.
	
	SerialisationTest( const std::string & Name, const std::string & Location )
	: Actor( Name ), Network( Name, Location )
	{	}
	
	// The default and copy constructors are explicitly deleted
	
	SerialisationTest( void ) = delete;
  SerialisationTest( const SerialisationTest & Other ) = delete;

  // The destructor is public and virtual to allow the base class to be 
  // correctly destroyed.

public:
	
	virtual ~SerialisationTest( void )
	{ }
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
		
		virtual Theron::SerialMessage::Payload 
		Serialize( void ) const override
		{
			return std::string("AskGuru");
		}
		
		// De-serialising means just to check if the payload equals the expected
		// class type.
		
		virtual bool 
		Deserialize( const Theron::SerialMessage::Payload & Payload ) override
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
		
		virtual Theron::SerialMessage::Payload 
		Serialize( void ) const override
		{
			std::ostringstream Message;
			
			Message << "RollDice " << NumberOfRolls;
			
			return Message.str();
		}
		
		// De-serialisation follows the same pattern as above, with the number of 
		// rolls read as the second element of the payload if the command is 
		// matching.
		
		virtual bool Deserialize( 
						const Theron::SerialMessage::Payload & Payload ) override
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
		
		virtual Theron::SerialMessage::Payload Serialize( void ) const override
		{
			return std::string("WordsOfWisdom ") + *this;
		}
		
		// De-serialising the message requires first a test to verify that the 
		// message received is really a Words of Wisdom message, and then set the 
		// content string
		
		virtual bool 
		Deserialize( const Theron::SerialMessage::Payload & Payload ) override
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
		
		virtual Theron::SerialMessage::Payload Serialize( void ) const override
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
		
		virtual bool 
		Deserialize( const Theron::SerialMessage::Payload & Payload ) override
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
	// Applications supporting communication must declare a network endpoint to
	// host the communication layer servers. The template argument to the network
	// endpoint is a technology dependent class derived from the Network. 
	
	Theron::NetworkEndPoint< SerialisationTest > 
		TheEndPoint( "EndPoint", "localhost" );
	
	// The Console Print Server ensures that the output from the actors is being 
	// serialised and arrives in order on the console screen.
		
	Theron::ConsolePrintServer TheConsole( &std::cout,"CoutServer" );

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
	// a local actor. First a pointer to the session server must be obtained 
	// from the endpoint. 
	
	auto TheNetwork = 	TheEndPoint.Pointer< DummyNetworkLayer >
											( Theron::Network::Layer::Network );
	
	TheNetwork->ForwardNetworkMessage( FirstWorker.GetAddress(), 
																     SecondWorker.GetAddress(), 
																     "AskGuru" );
	
	TheNetwork->ForwardNetworkMessage( FirstWorker.GetAddress(), 
																		 SecondWorker.GetAddress(), 
																		 "RollDice 5" );

	TheNetwork->ForwardNetworkMessage( Theron::Address("Remote_Actor"), 
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
	
	TheNetwork->ForwardNetworkMessage( Theron::Address("Remote_Actor"), 
																		 FirstWorker.GetAddress(), 
																		 "AskGuru" );
	
	TheNetwork->ForwardNetworkMessage( Theron::Address("Remote_Actor"), 
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
	// that all actors on all endpoints have finished processing. This is why 
	// the technology dependent network class (here Serialisation Test) has 
	// to provide a shut down message handler.
	
	 Theron::Actor::WaitForGlobalTermination();
	 
	// Everything should be OK, and so it is just to exit happily after a brief 
  // wait because it could be that an actor has served all messages, but other
  // IO operations has not been completed.
	 
  std::this_thread::sleep_for( std::chrono::seconds(1) );
	
	return EXIT_SUCCESS;
}
