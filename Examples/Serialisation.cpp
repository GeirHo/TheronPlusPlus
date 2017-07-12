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

#include <strstream>									// Various output

#include <boost/algorithm/string.hpp> // To convert to upper-case 

#include "Actor.hpp"									// The Actor framework
#include "PresentationLayer.hpp"	  	// The presentation layer actor extension
#include "ConsolePrint.hpp"						// Sequential output

/*=============================================================================

 Simple Session Layer

=============================================================================*/
//
// The simple session layer provides a message handler to receive outbound 
// messages in serialised format, and then prints the message. 

class SimpleSessionLayer : public Theron::Actor
{
public:
	
  void OutboundMessage( 
		   const Theron::PresentationLayer::SerialMessage & TheMessage,
			 const Theron::Address From                     )
	{
		Theron::ConsolePrint LogMessage;
		
		LogMessage << "[ OUTBOUND: ] " << TheMessage.GetPayload() << " From : "
		           << From.AsString() << std::endl;
							 
	}
	
	// The constructor simply registers this message handler.
	
	SimpleSessionLayer( void ) : Actor()
	{
		RegisterHandler( this, &SimpleSessionLayer::OutboundMessage );
	}
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
	// Request message
	// ---------------------------------------------------------------------------
	//
	// The first message is a request for the worker to return the appropriate 
	// response message. There are two response messages: One returning a string,
	// and the other returning some random number.

	class Request : public Theron::PresentationLayer::Serializeable
	{
	public:
		
		enum class Types
		{
			Wisdom,
			RollDice
		};
		
		// The value is stored privately to ensure that it will only be available 
		// to the request class and its methods.
		
	private:
		
		Types TheCommand;
		
		// The dice command can be followed by a number of times the dice should 
		// be rolled.
		
		unsigned int NumberOfRolls;
		
		// Serialising the message means just writing the command and the possible 
		// option values.
		
		virtual std::string Serialize( void ) const
		{
			std::ostringstream SerialMessage;
			
			SerialMessage << "REQUEST ";
			
			if ( TheCommand == Types::Wisdom )
				SerialMessage << "Wisdom";
			else
				SerialMessage << "RollDice " << NumberOfRolls;
			
			return SerialMessage.str();
		}
		
		// De-serialising requires to convert the command into one of the pre-
		// defined types of requests and then initialise the message fields.
		
		virtual bool Deserialize( 
						const Theron::PresentationLayer::SerializedPayload & Payload )
		{
			// The string to command map is initialised first
			
			static std::map< std::string, Types > CommandMap =
			{ { "WISDOM", 	Types::Wisdom 	},
				{ "ROLLDICE", Types::RollDice }	};
			
			// Then the message fields are decoded one by one.
				
			std::istringstream SerialMessage( Payload );
			std::string 			 Command;
			
			SerialMessage >> Command;
			boost::to_upper( Command );
			
			if ( Command == "REQUEST" )
		  {
				SerialMessage >> Command;
				boost::to_upper( Command );
				
				switch ( CommandMap.at( Command ) )
				{
					case Types::RollDice :
						TheCommand = Types::RollDice;
						SerialMessage >> NumberOfRolls;
						break;
					case Types::Wisdom :
						TheCommand = Types::Wisdom;
						NumberOfRolls = 0;
						break;
					default:
						break; // Nothing to do!
				};
				
				return true;
			}
			else 
				return false;
		}
		
	};
	
}


