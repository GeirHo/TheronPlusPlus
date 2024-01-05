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

Author and Copyright: Geir Horn, University of Oslo
Contact: Geir.Horn@mn.uio.no
License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)

Revisions:
  Geir Horn: Put the class under the Theron name space,
		   Added automatic actor naming by default
		   Added ostream constructor argument
		   Drain functionality moved to the destructor
		   Stream will use the server's execution framework
       Added exception for missing server when console print is created
=============================================================================*/

#ifndef CONSOLE_PRINT
#define CONSOLE_PRINT

#include <string>														// Standard strings
#include <iostream>													// For doing the output and input
#include <sstream>													// Formatted strings
#include <memory>														// For shared pointers
#include <type_traits>											// For compile time checks
#include <stdexcept>												// Standard exceptions
#include <source_location>                  // Improved reporting of errors

#include <syncstream>                       // Replacing it all with standard

#include "Actor.hpp"												// The Theron++ framework
#include "StandardFallbackHandler.hpp"			// Debugging and error handling

#undef max
#undef min

namespace Theron
{

/*****************************************************************************
 The print server actor

 This is almost too trivial as it simply register the output handler for the
 incoming string which will print the string when it is called. The agents
 can either send std::string messages directly to this server by using its 
 agent name given by the static Address function, or through the child
 agent stream class defined below which automates much of the work.

******************************************************************************/

class [[deprecated("Console print server should not be used only use ConsoleOutput")]]
ConsolePrintServer : public virtual Actor,
										 public virtual StandardFallbackHandler
{
public:

  // The print object could have been an actor in its own right, but often it 
  // is constructed for a single line of output, and it will never receive any
  // messages. Hence the overhead of an actor is significant for no benefits.
  // The important thing is that the output string is sent to the console print
  // server since some actors may send messages directly with with no Print 
  // object. For this there is a pointer to the print server, and this is used 
  // by the print object to access the send function on the server itself, i.e.
  // messages from print objects will appear as sent by the server to itself.
  
private:
  
  static ConsolePrintServer * TheServer;
  
  // Since the access to this pointer should be restricted to the console 
  // print stream it is declared as a friend.
  
  friend class ConsolePrint;
  
  // Termination is handled by the destructor creating a termination receiver
  // that will wait for the normal message handler to indicate that all 
  // messages have been printed. However, it should be noted that this 
  // approach is fragile if other actors are still running sending messages 
  // to this print server. To be on the safe side, it should be the first 
  // actor created, because then it will be the last actor destroyed.
  
  class Terminator : public Receiver
  {
  private:
      void DrainQueueConfirmation ( const bool & Confirmation, 
																    const Address TheServer )
      {};	// We simply ignore everything
  public:
      Terminator (void) : Receiver()
      {
			  RegisterHandler( this, &Terminator::DrainQueueConfirmation );
      };
  };

  // A shared pointer is initialised to this receiver by the destructor, 
  // and if this pointer is assigned it is an indication that the server is 
  // terminating and 'true' should be sent to the receiver when there are no
  // more messages in the queue.

  std::shared_ptr< Terminator > TerminationPhase;
  
  // The actual output is sent to a stream provided to the constructor to allow
  // the stream to be set to cout or cerr or a file in one place.
  
  std::ostream * OutputStream;

  // Handler function to print the content of the string

  void PrintString ( const std::string & message, const Address sender )
  {
      *OutputStream << message;

      if ( TerminationPhase )
	      Send( true, TerminationPhase->GetAddress() );
  };
	
	// Initialiser to avoid duplicating functionality in the constructor and the 
	// compatibility constructor.
	
	inline void Initialise( void )
	{
		if ( TheServer != nullptr )
 		{
			std::ostringstream ErrorMessage;
      std::source_location Location = std::source_location::current();
			
			ErrorMessage << Location.file_name() << " at line " << Location.line() 
                   << " in function "<< Location.function_name() << ": "
									 << "Only one Console Print Server can be used";
			
			throw std::logic_error( ErrorMessage.str() );
		}
		
    TheServer = this;
    RegisterHandler(this, &ConsolePrintServer::PrintString );		
	}

public:
    
  // The constructor initialises the actor, and registers the 
  // server function. It takes an optional actor name, and if this is not 
  // given the framework will automatically generate a unique name. It is 
  // strongly recommended that a name is given for debugging purposes. Note
  // that in order to capture an automatically constructed actor address, the 
  // global server name is initialised from the actor's own address.	
	
  ConsolePrintServer ( std::ostream * Output = &std::cout,
								       const std::string & TheName = std::string() )
  : Actor( TheName ),
    StandardFallbackHandler( Actor::GetAddress().AsString() ),
    OutputStream( Output )
  {
		Initialise();
  };

  ConsolePrintServer ( Theron::Framework & TheFramework, 
								       std::ostream * Output = &std::cout,
								       const std::string & TheName = std::string() )
  : Actor( TheFramework, ( TheName.empty() ? nullptr : TheName.data() ) ),
    StandardFallbackHandler( Actor::GetAddress().AsString() ),
    OutputStream( Output )
	{ 
		Initialise();
	}

  // If there are outstanding messages pending, the destructor must create 
  // the terminator receiver and wait for it to receive the information 
  // from the message handler that there are no more outstanding messages.

  virtual ~ConsolePrintServer( void )
  {
	  TerminationPhase = std::make_shared< Terminator >();
    
		while ( GetNumQueuedMessages() > 0 )
      TerminationPhase->Wait();
    
    TheServer = nullptr;
  }
};

/*****************************************************************************
The Console Print ostream

Each actor that wants to do output to the console may instantiate an 
object of this stream and write to it as any normal stream. It will buffer
all received input into the associated string and when the application calls
either the "endl" operator, the "flush" method, or de-constructs the object,
the content of the string will be forwarded as a message to the print server.

Essentially, most of this is standard functionality of the ostream, so the only
thing we need to care about is to improve the flush function and to make sure
we send the stream if its length is larger than zero.

******************************************************************************/
/*
class [[deprecated("Use ConsoleOutput instead of ConsolePrint") ]]
ConsolePrint : public virtual std::ostringstream
{
private:
	
	// The address of the print server is cached to make it faster to send the 
	// string to the server.
	
	Address TheConsole;
	
public:

    // The constructor is just empty since the actual messaging is handled
    // by the flush function or the destructor below.

    ConsolePrint (void ) 
    : std::ostringstream()
    { 	
			TheConsole = ConsolePrintServer::TheServer->GetAddress();

      if( TheConsole == Address() )
      {
        std::ostringstream ErrorMessage;
        std::source_location Location = std::source_location::current();
			
			  ErrorMessage << Location.file_name() << " at line " << Location.line() 
                     << " in function "<< Location.function_name() << ": "
					  				 << "The Console Print Server must be created prior to "
                     << " instantiating the Console Print output stream";
			
			  throw std::logic_error( ErrorMessage.str() );
      }
    };

    // The flush method sends the content of the stream to the print 
    // server and resets the buffer so that the stream can be reused if 
    // needed.

    std::ostringstream * flush( void )
    {
			if ( str().length() > 0 )
			{
			  ConsolePrintServer::TheServer->Send( std::string( str() ), TheConsole );

			  // Clear the string

			  clear(); str("");
			}
	
			return this;
    };
	    
    // The destructor does the same thing, except that it will not need to 
    // clear he buffer as this is done when the ostringstream is destroyed

    virtual ~ConsolePrint ( void )
    {
			if ( str().length() > 0 )
			  ConsolePrintServer::TheServer->Send( std::string( str() ), TheConsole );
    };
};
*/
class ConsoleOutput 
: public std::osyncstream
{
public:

  ConsoleOutput( void )
  : std::osyncstream( std::cout )
  {}
};

class [[deprecated("Use ConsoleOutput instead of ConsolePrint") ]]
ConsolePrint : public ConsoleOutput
{
public:
  ConsolePrint() = default;
};

}       // End name space Theron
#endif  // CONSOLE_PRINT
