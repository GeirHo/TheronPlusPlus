/*=============================================================================
Actor

Author and Copyright: Geir Horn, 2017
License: LGPL 3.0
=============================================================================*/

#include "Actor.hpp"

/*=============================================================================

 Actor identification (alias Framework)

=============================================================================*/

// Static members shared by all actors

std::atomic< Theron::Actor::Identification::IDType >
				Theron::Actor::Identification::TotalActorsCreated;

std::unordered_map< std::string, Theron::Actor::Identification * >
				Theron::Actor::Identification::ActorsByName;
				
std::unordered_map< Theron::Actor::Identification::IDType, 
										Theron::Actor::Identification * >
				Theron::Actor::Identification::ActorsByID;

std::mutex Theron::Actor::Identification::InformationAccess;

// -----------------------------------------------------------------------------
// Static functions 
// -----------------------------------------------------------------------------
//
// The static function to return the actor pointer based on a an address will
// throw an invalid argument if the given address is Null

Theron::Actor * Theron::Actor::Identification::GetActor( 
																			  Theron::Actor::Address & ActorAddress )
{
	if ( ActorAddress )
		return ActorAddress.TheActor->ActorPointer;
	else
		throw std::invalid_argument( "Invalid actor address" );
}

// The first lookup function by name simply acquires the lock and then 
// constructs the address based on the outcome of this lookup. Note that this 
// may result in an invalid address if the given actor name is not found.

Theron::Actor::Address Theron::Actor::Identification::Lookup(
																									const std::string ActorName )
{
	std::lock_guard< std::mutex > Lock( InformationAccess );
	
	auto TheActor = ActorsByName.find( ActorName );
	
	if ( TheActor == ActorsByName.end() )
		return Theron::Actor::Address();
	else
		return Theron::Actor::Address( TheActor->second );
}

// The second lookup function is almost identical except that it uses the ID 
// map to find the actor.

Theron::Actor::Address Theron::Actor::Identification::Lookup(
																														const IDType TheID )
{
	std::lock_guard< std::mutex > Lock( InformationAccess );
	
	auto TheActor = ActorsByID.find( TheID );
	
	if ( TheActor == ActorsByID.end() )
		return Theron::Actor::Address();
	else
		return Theron::Actor::Address( TheActor->second );
}

// The function setting the session layer will throw a logic error if another 
// actor already has claimed the role of a session layer, and the given pointer
// is not null, i.e. that this is invoked by an actor currently acting as the 
// session layer, but about to close.

void Theron::Actor::RemoteIdentity::SetSessionLayerServer( 
																											Theron::Actor * TheSever )
{
  if ( TheSever == nullptr )
	{
		// The session layer is de-registering and all external references to this 
		// session layer server should be removed - with no session layer server
		// it is not possible to communicate externally and the external references
		// should be invalidated.
	}
	else if ( TheSessionLayerServer == nullptr  )
		TheSessionLayerServer = TheSever;
	else 
	{
		std::ostringstream ErrorMessage;
		 
		ErrorMessage << "The session layer server is already set to actor "
								 << TheSessionLayerServer->ActorID.Name;
								 
	  throw std::logic_error( ErrorMessage.str() );
	}
		
}

// -----------------------------------------------------------------------------
// virtual functions 
// -----------------------------------------------------------------------------

void Theron::Actor::EndpointIdentity::Register( Address * NewAddress )
{
	std::lock_guard< std::recursive_mutex > Lock( AddressAccess );
	Addresses.insert( NewAddress );
}

void Theron::Actor::EndpointIdentity::DeRegister( Address * OldAddress )
{
	std::lock_guard< std::recursive_mutex > Lock( AddressAccess );
	Addresses.erase( OldAddress );
}

void Theron::Actor::RemoteIdentity::Register( Address * NewAddress )
{
	++NumberOfAddresses;
}

void Theron::Actor::RemoteIdentity::DeRegister( Address * OldAddress )
{
	if ( NumberOfAddresses > 0 )
		--NumberOfAddresses;
}

// -----------------------------------------------------------------------------
// Constructor and Destructor 
// -----------------------------------------------------------------------------
//
// The constructor of the identification simply stores the name in 

Theron::Actor::EndpointIdentity::EndpointIdentity( 
		Theron::Actor * TheActor, const std::string & ActorName )
	: Identification( TheActor, ActorName ),
	  Addresses(), AddressAccess()
{
	std::lock_guard< std::mutex > Lock( InformationAccess );
	
	auto Outcome = ActorsByName.emplace( Name, TheActor );
	
	if ( Outcome.second != true )
	{
		std::ostringstream ErrorMessage;
		
		ErrorMessage << "An actor with the name " << Name 
								 << " does already exist!";
		
		throw std::invalid_argument( ErrorMessage.str() );
	}
	
	ActorsByID.emplace( NumericalID, TheActor );
}

// The destructor first invalidates all addresses that references this actor,
// and then removes the actor from the two maps. The lock is acquired only 
// before the second part of the destructor

Theron::Actor::EndpointIdentity::~EndpointIdentity()
{

	// The address registry must be locked for access from this thread. However,
	// as addresses are invalidated, they will de-register which implies that 
	// the lock will be acquired also in the de-registration function. It will 
	// be from the same thread, but it will block unless the mutex accepts 
	// multiple locks.
	
	std::lock_guard< std::recursive_mutex > AddressLock( AddressAccess ); 
	
	// A standard for loop cannot be used to invalidate the addresses since 
	// the invalidation function will call back and remove the address entry 
	// from the address set. Instead, the first element will be invalidated until
	// the set is empty. The assignment is necessary for the compiler to 
	// understand that the returned iterator should not be constant. 
	
	while ( ! Addresses.empty() )
  {
		Address * TheAddress = *(Addresses.begin());
		TheAddress->Invalidate();
	}
	
	std::lock_guard< std::mutex > InformationLock( InformationAccess );
	
	ActorsByName.erase( Name 				);
	ActorsByID.erase  ( NumericalID );
}

// The constructor for the remote identity stores the identity in the registry 
// for actors by name if the Session Layer server is set. Otherwise it will 
// throw a logic error since remote addresses cannot be used without a session
// layer server. 
// 
// It is similar to the end point identity in that it will check that there is 
// no actor by this name already, even though this test should not be necessary
// it is included as an additional precaution. However, it will not store 
// the ID because the numerical ID is valid only on this endpoint, and it 
// will check the availability of the Session Server as a pre-requisite.

Theron::Actor::RemoteIdentity::RemoteIdentity( const std::string & ActorName )
: Identification( TheSessionLayerServer, ActorName )
{

	if ( TheSessionLayerServer != nullptr )
  {
		std::lock_guard< std::mutex > Lock( InformationAccess );
		
		auto Outcome = ActorsByName.emplace( Name, this );
		
		if ( Outcome.second != true )
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << "An actor with the name " << Name 
									 << " does already exist!";
			
			throw std::invalid_argument( ErrorMessage.str() );
		}
	}
	else
		throw std::logic_error("Remote actor IDs requires a Session Layer Server");
}
