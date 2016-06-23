/*=============================================================================
Console Print

Since the Theron framework is multi-threaded the different actors should not
print to cout since the output could then be just interleaved garbage. Instead
a print server is defined and the actors will send strings to this server in 
order to provide the output. Note that this also allows the console to run in 
a different framework and on a different host from the other agents, should
that be desired (currently not supported by the implementation).

The implementation has two parts: The first is the theron agent that takes the
incoming string and then simply prints this on "cout". The second part is 
a ostream class that can be used as the output stream instead of "cout" 
where the printing is needed. Output to this stream is terminated either 
with the manipulator "endl" or with a flush, each of which sends the stream
to the print agent and clears the local buffer. Note that one of the two must
be given for the formatted output to reach the console.

Author: Geir Horn, 2013
Lisence: LGPL 3.0
=============================================================================*/

#ifndef CONSOLE_PRINT
#define CONSOLE_PRINT

#include <limits>
#include <string>
#include <iostream>
#include <sstream>
#include <Theron/Theron.h>

#undef max
#undef min

namespace Theron
{

/*****************************************************************************
 The print server actor

 This is almost too trivial as it simply register the output handler for the
 incoming string which will print the string when it is called. The agents
 can either send std::string messages directly to this server by using its 
 agent name at Theron::Address("ConsolePrintServer"), or through the child
 agent stream class defined below which automates much of the work.

 The print server is used by agents across different endpoints and in general
 it is impossible to know when the last message has been handled by the 
 server. A working actor will normally have a way to find out that it has 
 completed its tasks, and then terminate or deregister from the registry if
 that utility class is used. However for the print actor there is no way to 
 know that the last message has been printed. 

 The mechanism offered to handle this situation is the following:

 1) When the main thread hosting the print server object is about to close
    (knowing that all other agents have finished), it calls the Drain 
	function on the print server. 
 2) The Drain function creates a receiver object, and stores that address 
    in the print server.
 3) When the unhandled message counter reaches zero, the print server 
    sends a dummy message to the waiting receiver and the Drain function
	terminates.
 
******************************************************************************/

class ConsolePrintServer : public Theron::Actor
{
private:

    // A flag for the termination phase

    bool TerminationPhase;

    // The counter for unhandled messages. By default initialised to a very
    // big number of messages since is is decremented for all messages 
    // not only in the termination phase.

    Theron::uint32_t MessagesToGo;

    // Address of the receiver to signal when the last pending message has
    // been handled

    Theron::Address TheReceiver;

    // Handler function to print the content of the string

    void PrintString ( const std::string & message, 
		       const Theron::Address sender )
    {
	std::cout << message << std::endl;

	if ( TerminationPhase )
	  if ( --MessagesToGo == 0 )
		Send( true, TheReceiver );
    };

public:
	
    // The constructor initialises the actor, and registers the 
    // server function. Note that this print server has a name so that
    // it can be found by the output streams later.

    ConsolePrintServer ( Theron::Framework & TheFramework )
	    :  Theron::Actor(TheFramework, "ConsolePrintServer")
    {
	RegisterHandler(this, & ConsolePrintServer::PrintString );
	TerminationPhase = false;
	MessagesToGo     = std::numeric_limits<Theron::uint32_t>::max();
    };

    // The Drain function is supposed to be called on the actual console
    // print server object, and it initialises the class variables for 
    // termination and let the terminator wait for the right number of 
    // messages to be completely served.

    void Drain (void)
    {
	// The reciver class used for the termination

	class Terminator : public Theron::Receiver
	{
	private:
	    void DrainQueueConfirmation ( const bool & Confirmation, 
 				          const Theron::Address TheServer )
	    {};	// We simply ignore everything
	public:
	    Terminator (void) : Receiver()
	    {
		RegisterHandler( this, &Terminator::DrainQueueConfirmation );
	    };
	} AllServed;
	
	TerminationPhase = true;
	TheReceiver      = AllServed.GetAddress();
	MessagesToGo     = GetNumQueuedMessages();

	if ( MessagesToGo > 0 ) AllServed.Wait();
    };

    // It should really not be necessary to call the drain function directly,
    // as one should rather let the destructor of the print server do the 
    // drain - only when the destructor is called, we will be certain that 
    // there will be no more messages generated for the print server.

    ~ConsolePrintServer( void )
    {
      if ( GetNumQueuedMessages() > 0 ) Drain();
    };
};

/*****************************************************************************
The ConsolePrint ostream

Each actor that wants to do output to the console may instantiate an 
object of this stream and write to it as any normal stream. It will buffer
all received input into the associated string and when the application calls
either the "endl" operator, the "flush" method, or deconstructs the object,
the content of the string will be forwarded as a message to the print server.

The stream is created by "ConsolePrint MySteamName( GetFramework() );" so that 
it will run in the same framework as its owner actor.

Essentially, most of this is standard functionality of the ostream, so the only
thing we need to care about is to improve the flush function and to make sure
we send the stream if its length is larger than zero.

******************************************************************************/

class ConsolePrint : public std::ostringstream, public Theron::Actor
{
private:

    // We cache the address of the print server - note that it is assumed
    // that there is only one - so that we avoid a possibly more expensive 
    // lookup if the strema is used for multiple messages.

    Theron::Address TheConsole;

public:

    // The constructor is just empty since the actual messaging is handled
    // by the flush function or the destructor below.

    ConsolePrint (Theron::Framework & TheFramework) 
    	: std::ostringstream(), Theron::Actor( TheFramework )
    { 	
	TheConsole = Theron::Address("ConsolePrintServer");
    };

    // The flush method sends the content of the stream to the print 
    // server and resets the buffer so that the stream can be reused if 
    // needed.

    std::ostringstream * flush( void )
    {
	if ( str().length() > 0 )
	{
	  Send( std::string( str() ), TheConsole );

	  // Clear the string

	  clear();
	  str("");
	  // seekp(0);
	}
	
	return this;
    };
	    
    // The destructor does the same thing, except that it will not need to 
    // clear he buffer as this is done when the ostringstream is destroyed

    ~ConsolePrint ( void )
    {
	if ( str().length() > 0 )
	  Send( std::string( str() ), TheConsole );
    };
};

}       // End name space Theron
#endif  // CONSOLE_PRINT
