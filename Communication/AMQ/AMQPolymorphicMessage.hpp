/*==============================================================================
AMQ Polymorphic message

The AMQ polymorphic message is just a specialisation of the generic polymorphic
message type with the payload being a shared pointer to an Qpid AMQ message
object. 

Author and Copyright: Geir Horn, University of Oslo
License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
==============================================================================*/

#ifndef THERON_AMQ_POLYMORPHIC_MESSAGE
#define THERON_AMQ_POLYMORPHIC_MESSAGE

// Standard headers

#include <memory>        // For smart pointers

// Qpid Proton headers

#include <proton/message.hpp>

// Theron++ header files

#include "Communication/PolymorphicMessage.hpp"

namespace Theron::AMQ
{

class PolymorphicMessage
: public Theron::PolymorphicMessage< std::shared_ptr< proton::message > >
{
public:
  
  PolymorphicMessage() = default;
  virtual ~PolymorphicMessage() = default;
}
  
}      // end name space Theron AMQ
#endif // THERON_AMQ_POLYMORPHIC_MESSAGE 
