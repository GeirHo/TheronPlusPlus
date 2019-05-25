/*=============================================================================
  Network End Point

  Author: Geir Horn, University of Oslo, 2015-2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/


#include <sstream>    // Formatted error messages
#include <stdexcept>  // To throw exceptions if no shut down receiver

#include "NetworkEndpoint.hpp"

/*=============================================================================

  Static variables

=============================================================================*/

// The pointers to the OSI stack layer servers are kept in a static map in order
// for other actors to get the relevant addresses to register and de-register
// with these servers if necessary.

std::map< Theron::Network::Layer, std::shared_ptr< Theron::Actor > >
				  Theron::Network::CommunicationActor;

// There is also a place holder for the global endpoint allowing other actors
// to obtain the address of the endpoint without having a direct reference to
// the network endpoint actor.

Theron::Actor * Theron::Network::ThisNetworkEndpoint = nullptr;

