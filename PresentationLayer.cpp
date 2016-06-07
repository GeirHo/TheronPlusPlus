/*=============================================================================
  Presentation Layer

  Author: Geir Horn, University of Oslo, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include "PresentationLayer.hpp"

namespace Theron {
  
// First we declare the static pointer variable
  
PresentationLayer * PresentationLayer::Pointer;

/*****************************************************************************
  Catching external messages
  
  A message that is sent by either the Send method on an actor or by the 
  Send method on the framework ends up in the framework's Send Internal method 
  in the internal IMessage format. The Send Internal function uses the 
  Endpoint's lookup function to see if this actor exists on the local Endpoint.
  If this look up fails, the message is forwarded to the Endpoint's Request 
  Send method. We re-define this method and since the implementation is in 
  the Endpoint's source file it is in the Theron lib file, which is one of the 
  last files seen, we hope that it will be used instead of the library defined
  function (so we do not have to temper with the library implementation).
******************************************************************************/

bool EndPoint::RequestSend( Detail::IMessage *const message, 
			    const Detail::String &name)
{
  // Check validity and pass to the Presentation Layer if legal
  
  if (message == 0 || name.IsNull())
    return false;
  else
  {
    PresentationLayer::Pointer->OutboundMessage( 
      message->GetMessageData(), message->GetMessageSize(),
      message->From(), Address( name.GetValue() ) );
    
    // Once the message has been transformed by this call to a serial message
    // its memory allocated by Theron must be freed. In the original code 
    // this is done by the network serving thread, but since we have our own
    // transmission system that thread is not needed. The following two lines 
    // have been copied from EndPoint::NetworkThreadProc()
   
    IAllocator *const allocator(AllocatorManager::GetCache());
    Detail::MessageCreator::Destroy(allocator, message);
    
    // Then we can reply that the sending was successful
    
    return true;
  }
}

} // End namespace Theron