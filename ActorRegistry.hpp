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

Authoor: Geir Horn, 2013
Lisence: LGPL 3.0
=============================================================================*/

#ifndef ACTOR_REGISTRY
#define ACTOR_REGISTRY

#include <set>
#include <map>
#include <string>
#include <Theron/Theron.h>

/******************************************************************************
 The Actor Registry
******************************************************************************/

namespace Theron
{

class ActorRegistry : public Theron::Actor
{
public:

  // The framework must be stored for the terminator objects, and 
  // there is an alias for this unique identifier

  typedef Theron::uint32_t FrameworkID;

  // The commands that can be used to register and deregister actors

  enum RegistryCommands
  {
    RegisterActor,
    DeRegisterActor,
    RegisterTerminator
  };

  // Terminators must be registered with their framework since a 
  // terminator is a receiver by default and it does not have a framework
  // so the framework part of its address is zero for all receivers, thus
  // the terminator must explicitly be associated with a given framework.
  // This field is ignored when an actor register. Note that the address is
  // not stored here since it is explicitly provided in the "from" field
  // when the registry receives the message.

  class RegistrationData
  {
  public:
    RegistryCommands Command;
    FrameworkID		 TheFramework;

    RegistrationData( RegistryCommands TheCommand, FrameworkID Framework )
    {
	    Command      = TheCommand;
	    TheFramework = Framework;
    };
  };

private:

  // We also give a name to the actor set

  typedef std::set< Theron::Address >  ActorSet;
  
  // The structure to hold the addresses of the active actors

  ActorSet ActiveActors;

  // A map is used to hold the terminators where the key field is the
  // address of the terminator, and the second field is an ID for the 
  // framework of the terminator. This to guarantee that the different
  // addresses are unique so that a terminator does not register twice.

  typedef std::pair< Theron::Address, FrameworkID > TerminatorID;

  std::map< Theron::Address, FrameworkID > Terminators;

  // Then there is a handler for the registration and deregistration of
  // actors and terminators - note that the command decides the type 
  // of object to register. 
  //
  // Note that the address itself cannot be sent between remote nodes,
  // but in this case we simply register the sender address so we are 
  // not dependent on serialising the actual address.

  void CommandHandler ( const ActorRegistry::RegistrationData &
							  TheRegistration,
					    const Theron::Address TheActor )
  {
    switch ( TheRegistration.Command )
    {
    case RegisterActor:
      ActiveActors.insert( TheActor );
      break;

    case DeRegisterActor:
      {   // Addigional scoping is needed by VC++2010
	  auto ActorPtr = ActiveActors.find( TheActor );

	  if ( ActorPtr != ActiveActors.end() )
		  ActiveActors.erase( ActorPtr );

	  // If this was the last active actor we inform all the 
	  // terminators that the game is over. We send the message to
	  // the terminator on the same framework as this registry as the
	  // last terminator since this may potentially kill also the 
	  // thread that runs this registry and preventing us from 
	  // sending the termination message to all terminators.

	  if ( ActiveActors.empty() )
	  {
	      auto ThisFramework  = GetAddress().GetFramework();
	      Theron::Address ThisTerminator = Theron::Address().Null();

	      // VC++ 2010 requires this to be written for each and the
	      // extra scoping is around the "else" part is also needed
	      // although the scoping does not harm for g++
	      //for each ( TerminatorID TheTerminator in Terminators )

	      for ( TerminatorID TheTerminator : Terminators )
	      {
		      if ( TheTerminator.second != ThisFramework )
			      Send( true, TheTerminator.first );
		      else
			      ThisTerminator = TheTerminator.first;
	      }

	      if ( ThisTerminator != Theron::Address().Null() )
		      Send( true, ThisTerminator );
	  };
      }
      break;

    case RegisterTerminator:
      // The Terminators have a handshake where the outcome of the
      // registration is returned as a message. This is important since
      // the terminator can in principle run on a remote node, and 
      // throwing an exception here will not be catched where the 
      // terminator is executing. The registration will fail if there 
      // is already a Terminator present with the given address.
      
      auto InsertResult = Terminators.insert( 
	      TerminatorID( TheActor, TheRegistration.TheFramework ) );	

      Send( InsertResult.second, TheActor ); // Confirm the registration
      break;
    };
  };

public:

  // The default constructor initialises the structures and registers the
  // message handler

  ActorRegistry( Theron::Framework & LocalFramework ) 
	  : Theron::Actor( LocalFramework, "ActorRegistry" ), 
	    ActiveActors(), Terminators()
  {
	  RegisterHandler( this, &ActorRegistry::CommandHandler );
  };

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
    TheActor->GetFramework().Send( RegistrationData( 
			    RegisterActor,
			    TheActor->GetAddress().GetFramework() ),
	    TheActor->GetAddress(), Theron::Address("ActorRegistry") );
  };

  // Deregistration is also stright forward. One could imagine that one
  // made a consistency test for an empty message queue for the calling
  // actor. However, after starting up an actor will only wake up to 
  // handle messages, so the deregistration will happen from one of its
  // message handlers. The message queue is therefore not empty before 
  // the message handler terminates, and testing here for an empty queueu
  // would be meaningless. It is up to the programmer to verify that all
  // invariants are met.

  static void Deregister ( Theron::Actor * TheActor )
  {
    TheActor->GetFramework().Send( ActorRegistry::RegistrationData(
			    DeRegisterActor,
			    TheActor->GetAddress().GetFramework() ),
	      TheActor->GetAddress(), Theron::Address("ActorRegistry") );
  };

};

/******************************************************************************
 The Terminator class

 The idea is that the terminator is an object that can be used in the main
 function on a node to prevent the main function to terminate before all 
 actors have deregistered. This implies two constraints:

 1) The ActorRegistry must be started before a Terminator is started
 2) If a terminator runs at the same physical node as the ActorRegistry,
    it should have the same framework identificator as the actor registry

 The last recuirement ensures that the registry will not call the local 
 terminate before all other Terminators have been terminated.

 The terminator is a simple receiver that register with the agent registry
 and then waits for the confirmation. After the successful registration, the 
 program may wait for the terminator to receive the final message from the 
 registry when the last agent deregister and the program may terminate. Its
 message handler simply tests the value of the received boolean, and throws
 an exception if it is false.

******************************************************************************/

class Terminator : public Theron::Receiver
{
protected:

	// The message handler

	void Handshake ( const bool & Status, 
					 const Theron::Address Sender )
	{
		if ( Status == false )
			throw std::string("Terminator error message received");
	};

public:

	// The constructor takes an agent as argument and "attaches" to the 
	// famework of that agent. The reason for using an agent and not the
	// framework is that the agent has a method to return the framework ID
	// whereas the framework itsef has no such possibility.

	Terminator ( Theron::Actor & PeerActor ) : Theron::Receiver()
	{
		RegisterHandler( this, &Terminator::Handshake );
		PeerActor.GetFramework().Send( ActorRegistry::RegistrationData( 
					ActorRegistry::RegisterTerminator,
					PeerActor.GetAddress().GetFramework() ),
			GetAddress(), Theron::Address("ActorRegistry")   );

		// We should now be able to wait for the reply from the registry
		// that this command was successful.

		Wait();
	};
};
}       // End namespace Theron
#endif  // ACTOR_REGISTRY
