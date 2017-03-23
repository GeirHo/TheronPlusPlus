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

Theron::Actor::ActorIdentification::IDType 
				Theron::Actor::ActorIdentification::TotalActorsCreated = 0;

std::unordered_map< std::string, Theron::Actor * >
				Theron::Actor::ActorIdentification::ActorsByName;
				
std::unordered_map<Theron::Actor::ActorIdentification::IDType, Theron::Actor *>
				Theron::Actor::ActorIdentification::ActorsByID;

std::mutex Theron::Actor::ActorIdentification::InformationAccess;

// -----------------------------------------------------------------------------
// Static functions 
// -----------------------------------------------------------------------------
//
// The static function to return the actor pointer based on a an address will
// throw an invalid argument if the given address is Null

Theron::Actor * Theron::Actor::ActorIdentification::GetActor( 
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

Theron::Actor::Address Theron::Actor::ActorIdentification::Lookup(
																									const std::string ActorName )
{
	std::lock_guard< std::mutex > Lock( InformationAccess );
	
	auto TheActor = ActorsByName.find( ActorName );
	
	if ( TheActor == ActorsByName.end() )
		return Theron::Actor::Address();
	else
		return Theron::Actor::Address( &(TheActor->second->ActorID) );
}

// The second lookup function is almost identical except that it uses the ID 
// map to find the actor.

Theron::Actor::Address Theron::Actor::ActorIdentification::Lookup(
																														const IDType TheID )
{
	std::lock_guard< std::mutex > Lock( InformationAccess );
	
	auto TheActor = ActorsByID.find( TheID );
	
	if ( TheActor == ActorsByID.end() )
		return Theron::Actor::Address();
	else
		return Theron::Actor::Address( &(TheActor->second->ActorID) );
}

// -----------------------------------------------------------------------------
// Destructor 
// -----------------------------------------------------------------------------
//
// The destructor first invalidates all addresses that references this actor,
// and then removes the actor from the two maps. The lock is acquired only 
// before the second part of the destructor

Theron::Actor::ActorIdentification::~ActorIdentification()
{
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
	
	std::lock_guard< std::mutex > Lock( InformationAccess );
	
	ActorsByName.erase( Name 				);
	ActorsByID.erase  ( NumericalID );
}

