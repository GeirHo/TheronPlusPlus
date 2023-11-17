/*==============================================================================
Zero Message Queue (ZMQ) Session layer

This file implements the functionality of the ZMQ session layer classes.
Please see the header file for details.

Author and Copyright: Geir Horn, University of Oslo, 2017-2019
License: LGPL 3.0
==============================================================================*/

#include "Communication/ZMQ/ZMQSessionLayer.hpp"

/*=============================================================================

 Actor address management

=============================================================================*/
//
// If an request for an address resolution comes in on the subscriber, it will
// forward a local actor inquiry to the local session layer. If the actor does
// exist on this endpoint, the session layer will return the inquiry message
// with the address of the actor set equal to the stored external address of
// the actor.

void Theron::ZeroMQ::SessionLayer::CheckActor(
	const ZeroMQ::NetworkLayer::LocalActorInquiry & TheRequest,
	const Address TheLinkServer )
{
	Address TheActor( TheRequest.ActorRequested.GetActorAddress() );

	if ( TheActor.IsLocalActor() )
  {
		auto ActorRecord = KnownActors.right.find( TheActor );

		if ( ActorRecord != KnownActors.right.end() )
			Send( NetworkLayer::LocalActorInquiry( ActorRecord->second,
																		         TheRequest.RequestingEndpoint ),
					  TheLinkServer );
	}
}

