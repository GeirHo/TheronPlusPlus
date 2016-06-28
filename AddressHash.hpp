/*=============================================================================
  Address hash
  
  This small utility header provides a hash function for a Theron Address 
  by using the standard hash function on the address' string representation.
  Unordered maps or sets are enabled using Theron Addresses by including this
  file in addition to the desired unordered function.
  
  
  Author: Geir Horn, University of Oslo, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef THERON_ADDRESS_HASH
#define THERON_ADDRESS_HASH

#include <functional>		// For the standard hash functions
#include <Theron/Address.h>	// For Theron Address

namespace std {
  
  template <>
  class hash< Theron::Address >
  {
  public:
    
    size_t operator() (const Theron::Address & TheAddress ) const
    {
      return std::hash< string >()( TheAddress.AsString() );
    }
    
  };
}

#endif // THERON_ADDRESS_HASH