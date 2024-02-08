###############################################################################
#
# Theron++
#
# Theron actors only require the actor header and source file. However, in
# practice one would probably use more of the utility code base. In particular,
# if the transparent communication is used, then multiple code files will be
# needed as it became infeasible to implement the communication technology
# interface classes as 'header only' libraries.
#
# The purpose of this make file is to define a target 'Library' that builds
# and archives all source files so that the use of Theron++ is just to set
# the include directory to the top level Theron++ and link against this library.
#
# In order to keep the code base clean from the compiler generated object files
# they are placed in a dedicated 'bin' directory and rebuilt as needed.
#
# Author: Geir Horn, University of Oslo, 2019
#
###############################################################################
#
# The first section covers the standard definitions and compiler flags. The
# code is standard C++, but may utilize features of the most recent standard
# draft and so it is important that the compiler supports the advanced features.
# The standard is explicitly given, and currently it is C++ 2017 version.
# Both Gnu C++ and CLang has been tested and can be used.

CXX = ccache g++

# The standard archive utility is used with the following flags:
# r = Replace an existing library entry with the new one
# u = Update only files if they are newer than the ones in the archive
# v = Verbose telling us what happens
# s = Create or update the library index (equivalent with ranlib)

ARFLAGS = ruvs

# The compiler is instructed to produce all warnings and provide output for
# the code to be debugged with GDB. This produces larger object files and if
# this is a concern the GDB switch can be dropped. -Wall to be added

GENERAL_OPTIONS = -c -Wall -std=c++23 -ggdb

# Optimisation -O3 is the highest level of optimisation and should be used
# with production code. -Og is the code optimising and offering debugging
# transparency and should be use while the code is under development

OPTIMISATION_FLAG = 

# It is useful to let the compiler generate the dependencies for the various
# files, and the following will produce .d files that can be included at the
# end. The -MMD flag is equivalent with -MD, but the latter will include system
# headers in the output (which we do not need here). The -MP includes an
# empty rule to create the dependencies so that make would not create any errors
# if the file name changes.

DEPENDENCY_FLAGS = -MMD -MP

# The combined flags for the compiler

CXXFLAGS = $(DEPENDENCY_FLAGS) $(OPTIMISATION_FLAG) $(GENERAL_OPTIONS)

# The linker flags include the use of POSIX threads as they are the underlying
# implementation of standard C++ threads and is explicitly needed on some
# systems.

LDFLAGS = -ggdb -D_DEBUG -pthread

#------------------------------------------------------------------------------
#
# Theron++ base file
#
#------------------------------------------------------------------------------
#
# The Theron include directory is rooted on this directory

THERON_INCLUDE = .

# The file locations are relative to the Theron++ base directory where this
# make file is located.

OBJECTS_DIR = Bin

# There is only one header file and one source file involved

ACTOR_HEADER  = Actor.hpp
ACTOR_SOURCE  = Actor.cpp
ACTOR_OBJECTS = ${ACTOR_SOURCE:.cpp=.o}

# The command to build the object files for the base files

$(OBJECTS_DIR)/%.o : %.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(INCLUDE_DIRECTORIES)

#------------------------------------------------------------------------------
#
# Utility files
#
#------------------------------------------------------------------------------
#
# The utilities are useful classes that may help in setting up a working
# actor system. Some of the classes are header only, and some of them requires
# accompanying object files.
#
# Note that the Console Print class is deprecated and the file ConsolePrint.cpp
# should only be included for backward compatibility (creates unnecessary 
# warnings otherwise)

UTILITY_DIR     = Utility
UTILITY_HEADERS = ActorRegistry.hpp AddressHash.hpp ConsolePrint.hpp \
	          EventHandler.hpp StandardFallbackHandler.hpp \
	          TerminationWatch.hpp WallClockEvent.hpp
UTILITY_SOURCE  = ActorRegistry.cpp EventHandler.cpp
UTILITY_OBJECTS = ${UTILITY_SOURCE:.cpp=.o}

# The command to build the object files for the utility files

$(OBJECTS_DIR)/%.o : $(UTILITY_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(INCLUDE_DIRECTORIES)

#------------------------------------------------------------------------------
#
# Transparent communication
#
#------------------------------------------------------------------------------
#
# There are common files defining the different communication layers to be used
# by the technology specific protocols.

COMMUNICATION_DIR     = Communication
COMMUNICATION_HEADERS = LinkMessage.hpp NetworkEndpoint.hpp \
						NetworkingActor.hpp NetworkLayer.hpp \
						PolymorphicMessage.hpp PresentationLayer.hpp \
						SessionLayer.hpp
COMMUNICATION_SOURCE  = NetworkEndpoint.cpp
COMMUNICATION_OBJECTS = ${COMMUNICATION_SOURCE:.cpp=.o}

# The command to build the object files for the communication files is
# similar to the command used for the utility files

$(OBJECTS_DIR)/%.o : $(COMMUNICATION_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(INCLUDE_DIRECTORIES)

#------------------------------------------------------------------------------
#
# Active Message Queue
#
#------------------------------------------------------------------------------
#
# The support for the Active Message Queue (AMQ) is based on the Qpid Proton
# AMQ API.

AMQ_DIR     = $(COMMUNICATION_DIR)/AMQ
AMQ_HEADERS = AMQEndpoint.hpp  AMQMessage.hpp AMQPresentationLayer.hpp \
			  AMQjson.hpp AMQNetworkLayer.hpp AMQSessionLayer.hpp
AMQ_SOURCE  = AMQjson.cpp AMQNetworkLayer.cpp AMQSessionLayer.cpp

AMQ_OBJECTS = ${AMQ_SOURCE:.cpp=.o}

# Then the build command is set to include these additional definitions.

$(OBJECTS_DIR)/%.o : $(AMQ_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(INCLUDE_DIRECTORIES)

#------------------------------------------------------------------------------
#
# Combined directives
#
#------------------------------------------------------------------------------
#

INCLUDE_DIRECTORIES = -I$(THERON_INCLUDE)
ALL_OBJECTS         = $(addprefix $(OBJECTS_DIR)/, $(ACTOR_OBJECTS) \
                      $(UTILITY_OBJECTS) $(COMMUNICATION_OBJECTS) \
                      $(AMQ_OBJECTS) )

###############################################################################
#
# Targets
#
###############################################################################
#
#
# Building the library

Theron++.a: $(ALL_OBJECTS)
	$(AR) $(ARFLAGS) Theron++.a $?

Library: Theron++.a

# Cleaning means deleting the object and dependencies and the library

clean:
	$(RM) Theron++.a $(OBJECTS_DIR)/*.o $(OBJECTS_DIR)/*.d

###############################################################################
#
# Dependencies
#
###############################################################################
#

-include $(ALL_OBJECTS:.o=.d)
