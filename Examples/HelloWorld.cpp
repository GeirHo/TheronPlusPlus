/*=============================================================================
  Hello World
  
  The purpose of this small programme is to illustrate how to use actors 
  collaboratively to construct a message to the console output using the 
  features of the Theron++ actor framework. 
  
  Note that this is a very complicated way of printing Hello World!, and this 
  is done to illustrate how more elaborate implementations can be made. The 
  implementation is also inefficient since the strings involved are copied 
  several times before the message is finally printed.
  
  This is compiled with 'make HelloWorld' and please see the makefile for 
  how to compile and build.
  
  Author and Copyright: Geir Horn, 2017
  License: LGPL 3.0
=============================================================================*/
  
#include <string>							// To manipulate strings
#include <vector>							// To store the partial strings
#include <iterator>						// To make the output more interesting
#include <algorithm>					// For the copy method

#include "Actor.hpp"					// The Actor definition
#include "ConsolePrint.hpp"   // Sequential output to the console

/*=============================================================================

 Messaging actor

=============================================================================*/

// The messaging actor takes a string as argument and a receiver of the composed
// message. When it receives a message, i.e. an object, it will add its string
// and forward the message to the next actor. It will report the actions to 
// the console. If the forwarding address is the Null address, it will print the 
// the combined message.
//
// It should be noted that the actor base class is 'virtual' and this is 
// strongly recommended to avoid the 'diamond inheritance problem' of object 
// oriented programming, see
// https://en.wikipedia.org/wiki/Multiple_inheritance#The_diamond_problem

class MessagingActor : public virtual Theron::Actor
{
public:
	
  // ---------------------------------------------------------------------------
  // The message
  // ---------------------------------------------------------------------------
  //
	// The message format supported by an actor is typically defined as a public
	// class since other actors communicating with this actor type will need to 
	// send this type of message. 
	//
	// In order to make the message more interesting, it will contain a standard
	// vector of standard strings, and an interface to operate on these strings.
	// It is defined complicated to show that a message can have any class format.
	
	class Message
	{
	private:
		
		std::vector< std::string > TheWisdoms;
		
	public:
		
		inline void AddPhrase( const std::string & TheWords )
		{ 
			TheWisdoms.push_back( TheWords ); 
		}
		
		inline void Print( std::ostream & Output )
		{ 
			std::ostream_iterator< std::string > OutStream( Output, " ");
			std::copy( TheWisdoms.begin(), TheWisdoms.end(), OutStream ); 
		}
		
		// There is one constructor that initiates an empty vector
		
		Message( void )
		: TheWisdoms()
		{ }
		
		// Then one that takes an initial string as the first phrase. Basically it
		// delegates the construction to the previous constructor and adds the 
		// phrase using the above function
		
		Message( const std::string & TheWords )
		: Message()
		{
			AddPhrase( TheWords );
		}
		
		// All messages must have a copy constructor to ensure that they are 
		// properly stored at the destination actor even if the source actor 
		// destroys the message. In this case it is easy to rely on the vector's 
		// copy constructor.
		
		Message( const Message & OtherMessage )
		: TheWisdoms( OtherMessage.TheWisdoms )
		{ }
	};
	
  // ---------------------------------------------------------------------------
  // The actor state - data it protects and maintains
  // ---------------------------------------------------------------------------
  //
	// The actor will, as stated in the introduction, store a string that will
	// be added to the message, and then the combined string will be forwarded 
	// to the next contributing actor, provided that this is not a Null address.
	
private:
	
	std::string     MyWisdom;
	Theron::Address NextContributor;

  // ---------------------------------------------------------------------------
  // The message handler
  // ---------------------------------------------------------------------------
  //	
	// Then a handler for this message type can be defined. It is a public 
	// handler for this example since it will be called from the outside on the 
	// first actor instantiation. In general one should not directly call a 
	// method on an actor as it should respond to incoming messages, and it may 
	// therefore be more common to declare the handlers as protected or private.
	
public:
	
	void ForwardOrPrint( const Message & TheCommunication, 
											 const Theron::Address Sender      )
	{
		// First our own wisdom is written out
		
		Theron::ConsolePrint OutputMessage;
		
		OutputMessage << "My wisdom is \"" << MyWisdom << "\"" << std::endl;
		
		// Then the message is extended with our wisdom. It is necessary to start
		// from a copy of the message since the given message is passed as a 
		// constant.
		
		Message AugmentedMessage( TheCommunication );
		
		AugmentedMessage.AddPhrase( MyWisdom );
		
		// If it is possible to route a message to the address stored as the 
		// next contributor, it will be done, otherwise the message will just 
		// be printed to the console print stream. Since the print function just 
		// concatenates the strings it is necessary to add a new line at the end 
		// to ensure that the output is correctly formatted.
		
		if ( NextContributor )
			Send( AugmentedMessage, NextContributor );
		else
		{
			AugmentedMessage.Print( OutputMessage );
			OutputMessage << std::endl;
		}
	}

  // ---------------------------------------------------------------------------
  // Constructor
  // ---------------------------------------------------------------------------
  //
  // The constructor takes the string to add and the next actor to process this 
  // string. This information is just stored. Then it registers the handler 
  // for the incoming message.
  
  MessagingActor( const std::string & WordsOfWisdom, 
									const Theron::Address & NextActor = Theron::Address::Null() )
	: Actor(), MyWisdom( WordsOfWisdom ), NextContributor( NextActor )
	{
		RegisterHandler( this, &MessagingActor::ForwardOrPrint );
	}
	
};

/*=============================================================================

 Main

=============================================================================*/

// The main instantiates the console print server to handle the console output,
// and then three actors, one for each part of the message.

int main(int argc, char **argv) 
{	
	// The console print server is set to produce the output to the standard 
	// output stream.
	
	Theron::ConsolePrintServer PrintServer(  &std::cout, 
																					"ConsolePrintServer" );
	
	// Then the three actors producing the parts of the output. They are 
	// constructed in inverse order so that the address of an actor already 
	// constructed can be passed to the one that will forward the message to 
	// it.
	
	MessagingActor FamousLastWords("World!", Theron::Address::Null() ),
								 TheHello( "Hello", FamousLastWords.GetAddress() ),
								 TheIntro( "Collectively we say:", TheHello.GetAddress() );
								 
  // The message exchange is started by some event. In this case is is the 
  // event that the first actor gets a message, and so it will be passed an 
  // empty message from the null actor since the handler ignores the sender
  // address anyway.
 
  TheIntro.ForwardOrPrint( MessagingActor::Message(), Theron::Address::Null() );
	
	// Finally, it is just to wait for all messages to be handled by all actors 
	// before main terminates
	
	Theron::Actor::WaitForGlobalTermination();
	
	return EXIT_SUCCESS;
}
