/*=============================================================================
  Console Print

  The console print class defines some static variables, and this file is 
  basically a placeholder for these variables.
       
  Author: Geir Horn, University of Oslo, 2016-2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include "ConsolePrint.hpp"

namespace Theron {
  
// There is also a pointer to the execution framework of the console print 
// server to be used when the console print stream is used outside of an actor
// and where it can be difficult to provide an execution framework.

ConsolePrintServer * ConsolePrintServer::TheServer = nullptr;

}  // End name space Theron
