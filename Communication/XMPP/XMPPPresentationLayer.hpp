/*=============================================================================
  Presentation Layer

  A special presentation layer for the XMPP transport is only needed to
  implement the virtual address function for the session layer address.

  Author and copyright: Geir Horn, University of Oslo, 2015 - 2020
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef THERON_XMPP_PRESENTATION_LAYER
#define THERON_XMPP_PRESENTATION_LAYER

#include "Actor.hpp"
#include "Utility/StandardFallbackHandler.hpp"
#include "Communication/PresentationLayer.hpp"

namespace Theron::XMPP
{
class PresentationLayer
: virtual public Actor,
  virtual public StandardFallbackHandler,
  public Theron::PresentationLayer
{
	// The only thing that needs to be added for the presentation layer is
	// the virtual function returning the address of the session layer, and
	// this is implemented in terms of the static function of the XMPP endpoint

protected:

	virtual Address SessionLayerAddress( void ) const override;

	// The constructor only takes the name of the server actor

public:

	PresentationLayer( const std::string & ServerName = "PresentationLayer"  )
	: Actor( ServerName ),
	  StandardFallbackHandler( Actor::GetAddress().AsString() ),
	  Theron::PresentationLayer( Actor::GetAddress().AsString() )
	{}

	virtual ~PresentationLayer()
	{}
};

}      // End of name space XMPP
#endif // THERON_XMPP_PRESENTATION_LAYER
