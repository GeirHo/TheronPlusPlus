/*==============================================================================
Scheduled Hello World

This is a small test programme to test scheduled objects, which many people 
wrongly think of as actors. Actors are objects that executes concurrently, and 
not all objects are well modelled as actors. Actors where historically objects 
that were executed. First by interrupt handlers on single threaded CPUs, and 
then by a scheduler maintaining a pool of threads for executing the actors. 
This is essentially putting a second level scheduler on top of the operating 
system scheduler, and the approach is abandoned in Theron++ where an actor is 
a real thread. 

Message passing among objects still makes sense in many situations, in 
particular for applications with a request-response communication pattern where
the objects do not need to be concurrently running, but where the can run in 
some limited form of parallelism. For this reason the Scheduled class is 
provided. 

This scheduler is all implemented in terms of actors. It maintains a set of 
worker actors that executes the objects that have pending messages. It has an 
intelligent queue length controller actor that looks after the number of workers
in order to maintain the backlog of pending messages under a certain threshold. 
Finally, the scheduler is itself an actor responding to messages created by the 
scheduled objects, which are not actors.

The implementation therefore serves by itself as an advanced example of how 
Theron++ can be used. This small test file just implements the hello world
example. There is an Hello object that sends a message to the World actor that 
responds adds its world to the composed message.

The Gnu Multi Precision (GMP) library is used and one should therefore link 
code built with scheduled objects with the necessary libraries

-lgmpxx -lgmp

Author and Copyright: Geir Horn, 2018
License: LGPL 3.0
==============================================================================*/

#include <iostream>         // To print messages

#include "Scheduled.hpp"    // The scheduler

/*=============================================================================

 Oracle

=============================================================================*/
//
// For simplicity only one object is defined, and it uses a state flag 
// to decide whether it should respond with world or print out the message.

class Oracle : public Theron::Scheduled::Object
{
private:
	
	enum class State
	{
		Responder,
		Printer
	} 
	Role;
	
	// The message exchanged is simply a standard string, and there is a message 
	// handler for this string
	
	void ProcessString( const std::string & Request, 
											Theron::Scheduled::ObjectIdentifier Sender )
	{
		if ( Role == State::Responder )
		{
			std::string Response = Request + " World!";
			Send( Response, Sender );
		}
		else
			std::cout << "The message is: " << Request << std::endl;
	}
	
	// The default constructor simply sets the the role to be a responder and 
	// register the message handler
	
public:
	
	Oracle( void )
	: Object(), Role( State::Responder )
	{
		RegisterHandler( this, &Oracle::ProcessString );
	}
	
	// If an object identifier is given, the oracle assumes the role of a 
	// printer and sends the first part of the message to the given responder
	
	Oracle( const Theron::Scheduled::ObjectIdentifier & TheResponder )
	: Oracle()
	{
		Role = State::Printer;
		Send( std::string("Hello"), TheResponder );
	}
	
	// The destructor does nothing but is virtual to ensure that the scheduled 
	// object is properly destroyed.
	
	virtual ~Oracle( void )
	{ }
};

/*=============================================================================

 Main

=============================================================================*/
//
// The main routine just creates the two objects and waits for the system to 
// complete the message processing.

int main(int argc, char **argv) 
{
	Oracle TheWorld, TheHello( TheWorld.GetAddress() );
	
  Theron::Actor::WaitForGlobalTermination();
	
  return EXIT_SUCCESS;	
}
