/*=============================================================================
ZMQ Distribution

This example is similar to the Serialisation example except that it sends the 
messages across a Zero Message Queue (ZMQ) link on the local host loop back 
TCP socket. 

Two copies of the process must be started. Each process will start one worker 
actor whose name is given as a command line argument. Both workers will 
subscribe to the information about other peers, and once the other peer is 
reported as active, the worker will send one message for each type of request, 
and once both messages has been received, the worker will set its completed 
flag. 


Author and Copyright: Geir Horn, 2017
License: LGPL 3.0
=============================================================================*/

#include <iostream>

#include "ZeroMQ.hpp"

int main(int argc, char **argv) 
{
	Theron::ZeroMQ::TCPAddress TestAddress( "tcp://*:443" );
	
	std::cout << "Successfully converted the address to [" 
						<< TestAddress.AsString() << "] " << std::endl;
	
	return EXIT_SUCCESS;
}
