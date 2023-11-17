/*=============================================================================
  Network End Point

  This file defines the static Network class pointer and the static function 
  used to get the Theron actor framework address of the actors implementing 
  each of the network stack layers.
  
  Author and Copyright: Geir Horn, University of Oslo
  Contact: Geir.Horn@mn.uio.no
  License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
=============================================================================*/

#include "Communication/NetworkEndpoint.hpp"

namespace Theron
{
  const Network * Network::TheNetwork; 

	Address Network::GetAddress( Layer Role )
	{
	  Address LayerAddress;
	
	  switch( Role )
	  {
	    case Layer::Network:
	      LayerAddress = TheNetwork->NetworkLayerAddress();
	      break;
	    case Layer::Session:
	      LayerAddress = TheNetwork->SessionLayerAddress();
	      break;
	    case Layer::Presentation:
	     LayerAddress = TheNetwork->PresentationLayerAddress();
	     break;
    }
	
	  return LayerAddress;
  }

}