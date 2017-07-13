/*=============================================================================
Actor Registry

A general problem is to know when all actors have finished processing and 
the program can terminate. The Actor Registry class maintains a set of 
addresses to the actors registering with it by a registration message and 
waits for deregistration messages when the actors terminate. The registry actor
may itself terminate when the set of active actors is empty.

The whole idea with this class is to keep the main thread alive until all
agents have terminated. The issue is that the Theron framework executes the 
actors in several threads, but if all the actors have processed their messages 
and they are all waiting for messages from actors on another node or framework,
there is nothing more to do and in the worst case the host process may 
terminate. 

The actor registry is based on two principles: a registry agent which is 
itself a named actor, hence a global entity, and a set of termination receivers
that are typically instansiated in each main process on each of the computers
involved with executing the agent framework. Each terminator will receive 
one and only one message from the ActorRegistry, sent when the the last actor
unregister. 

Authoor: Geir Horn, 2013-2017
Lisence: LGPL 3.0
=============================================================================*/

#ifndef ACTOR_REGISTRY
#define ACTOR_REGISTRY

#include <set>
#include <string>
#include <stdexcept>
#include "Actor.hpp"
#include "StandardFallbackHandler.hpp"

/******************************************************************************
 The Actor Registry
******************************************************************************/

namespace Theron
{

class ActorRegistry : public virtual Theron::Actor,
											public virtual Theron::StandardFallbackHandler
{
private:
	
	// The name of the actor registry class is stored as a string accessible 
	// for all, and since there can only be one actor registry the constructor 
	// will throw if this string is not empty when the class is constructed.
	
	static std::string Name;
	
  // The commands that can be used to register and de-register actors

  enum class RegistryCommands
  {
    RegisterActor,
    DeRegisterActor,
    RegisterTerminator
  };

  // The structure to hold the addresses of the active actors

  std::set< Theron::Address > ActiveActors;

  // The address of the terminators are remembered in a separate set to be 
	// notified when the last actor de-register.

  std::set< Theron::Address > Terminators;

  // Then there is a handler for the registration and de-registration of
  // actors and terminators - note that the command decides the type 
  // of object to register. 

  void CommandHandler ( const RegistryCommands & Command,
										    const Theron::Address TheActor )
  {
    switch ( Command )
    {
			case RegistryCommands::RegisterActor:
	      ActiveActors.insert( TheActor );
	      break;

			case RegistryCommands::DeRegisterActor:
	      { 
				  auto ActorPtr = ActiveActors.find( TheActor );

				  if ( ActorPtr != ActiveActors.end() )
					  ActiveActors.erase( ActorPtr );

				  // If this was the last active actor we inform all the 
				  // terminators that the game is over. 

				  if ( ActiveActors.empty() )
			      for ( Address TheTerminator : Terminators )
				      Send( true, TheTerminator );
	      }
	      break;

			case RegistryCommands::RegisterTerminator:
	      Terminators.insert( TheActor );	
	      break;
    };
  };
	
public:

	// ---------------------------------------------------------------------------
  // Constructor and destructor 
  // ---------------------------------------------------------------------------
  // The default constructor initialises the structures and registers the
  // message handler

  ActorRegistry( Theron::Framework & LocalFramework, 
								 const std::string & RegistryName = "ActorRegistry" ) 
	  : Actor( LocalFramework, 
						 RegistryName.empty() ? nullptr : RegistryName.data() ),
	    StandardFallbackHandler( LocalFramework, GetAddress().AsString() ),
	    ActiveActors(), Terminators()
  {
		if ( Name.empty() )
			Name = RegistryName;
		else
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "Attempt to construct a second actor registry! "
									 << "There can be only one";
			
			throw std::logic_error( ErrorMessage.str() );
		}
			
		
	  RegisterHandler( this, &ActorRegistry::CommandHandler );		
  };

	// The destructor is virtual to allow virtual base classes to close properly
	
	virtual ~ActorRegistry()
	{}
	
	// ---------------------------------------------------------------------------
  // Registration of actors 
  // ---------------------------------------------------------------------------
  //
  // When an actor wants to register it needs to send a message to the
  // Actor Registry. The following two functions provide the
  // way to do so for the actors without needing to know anything about 
  // the way the actor registry is implemented. It is not documented in 
  // Theron whether the registry must be started before its address can
  // be looked up (I presume this is the case so the registry actor 
  // should be started first). 
  // 
  // For convenience, a pointer to the actor is taken as argument so that
  // 'this' may be used as a reference to the actor that registers. This 
  // is OK since these static functions can be used by any actor on any 
  // computer since they use only information that is available in the 
  // local memory space and interact with the (possibly remote) registry 
  // via a normal message. Since a static function lacks the this pointer
  // the registry's own send function cannot be used, but rather the send
  // function of the framework that owns the actor that wants to register.

  static void Register ( Theron::Actor * TheActor )
  {
    TheActor->GetFramework().Send( RegistryCommands::RegisterActor,
					    TheActor->GetAddress(), Theron::Address( Name.data() ) );
  };

  // De-registration is also straight forward. One could imagine that one
  // made a consistency test for an empty message queue for the calling
  // actor. However, after starting up an actor will only wake up to 
  // handle messages, so the de-registration will happen from one of its
  // message handlers. The message queue is therefore not empty before 
  // the message handler terminates, and testing here for an empty queue
  // would be meaningless. It is up to the programmer to verify that all
  // invariants are met.

  static void Deregister ( Theron::Actor * TheActor )
  {
    TheActor->GetFramework().Send( RegistryCommands::DeRegisterActor,
	      TheActor->GetAddress(), Theron::Address( Name.data() ) );
  };

	// ---------------------------------------------------------------------------
  // Terminator 
  // ---------------------------------------------------------------------------
  //
	// The idea is that the terminator is an object that can be used in the main
  // function on a node to prevent the main function to terminate before all 
  // actors have de-registered.
	//
	// The terminator class is a friend of the actor registry in order to access 
	// the name of the registry actor.
	
	friend class Terminator;
	
  // The terminator is a simple receiver that register with the agent registry
  // and then waits for the confirmation. After the successful registration, the 
  // program may wait for the terminator to receive the final message from the 
  // registry when the last agent de-register and the program may terminate. Its
  // message handler simply tests the value of the received boolean, and throws
  // an exception if it is false.
	
	class Terminator : public Theron::Receiver
	{
	protected:

		// The message handler

		void Handshake ( const bool & Status, 
										 const Theron::Address Sender )
		{
			if ( Status == false )
			{
				std::ostringstream ErrorMessage;
				
				ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
										 << "Terminator registration failed";
										 
				throw std::logic_error( ErrorMessage.str() );
			}
		};

	public:

		// The constructor register this receiver as a terminator, and expect that 
		// the wait function will be called explicitly on the terminator object 
		// later. It takes the framework as a parameter in order to be able to send
		// the registration message to the actor registry. The second parameter 
		// should be used if the actor registry is running on a different endpoint 
		// from where the terminator is running. In this case the static name will 
		// not be initialised and the message would go nowhere and crash Theron with 
		// unhanded message.

		Terminator ( Framework & TheFramework, 
								 Address RegistryAddress = Address(ActorRegistry::Name.data())) 
		: Receiver()
		{
			RegisterHandler( this, &Terminator::Handshake );
			
			TheFramework.Send( ActorRegistry::RegistryCommands::RegisterTerminator,
												 GetAddress(), RegistryAddress );
		}
	};
};

}       // End namespace Theron
#endif  // ACTOR_REGISTRY
