###############################################################################
#
# Theron++
#
# This file builds the the various examples of the Theron++ framework
#
# Author: Geir Horn, University of Oslo, 2017
#
###############################################################################

#
# Defining compiler and commands
#

CC = g++
#CC = clang++
RM = rm -f

# Location of the Theron++ framework relative to this make file and the code

THERON = ..

# Since some of the utility classes still includes "Theron/Theron.h" is it 
# necessary to define this file as a symbolic link to the "Actor.hpp" file of
# Theron++ For the author's installation it is done at the following location

OLD_THERON = ../../Theron/Include

# The transparent communication requires an implementation based on the 
# protocol being used, and potentially some extra libraries. The Zero Message 
# Queue (ZMQ) definitions needed are given below. It should be noted that the 
# shared zmqpp library is found under /usr/local/lib and so that directory must
# be read. There are many ways to do this, please see the excellent post at
# https://lonesysadmin.net/2013/02/22/error-while-loading-shared-libraries-cannot-open-shared-object-file/

ZMQ_INCLUDE = -I ../../ZeroMQ++/src/zmqpp
ZMQ_LIBS    = -lzmqpp -lzmq -lboost_system
ZMQ_HEADER  = ZeroMQ.hpp
ZMQ_SOURCE  = ../ZeroMQ.cpp
ZMQ_OBJECTS = ${ZMQ_SOURCE:.cpp=.o}

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

# Options 

GENERAL_OPTIONS = -c -Wall -std=c++1z -ggdb -D_DEBUG
INCLUDE_DIRECTORIES = -I. -I/usr/include -I$(THERON) -I$(ZMQ_INCLUDE)

# Putting it together as the actual options given to the compiler and the 
# linker. Note that pthread is needed on Linux systems since it seems to 
# be the underlying implementation of std::thread. 

CFLAGS = $(DEPENDENCY_FLAGS) $(OPTIMISATION_FLAG) $(GENERAL_OPTIONS)
LDFLAGS = -Wl,--allow-multiple-definition -ggdb -D_DEBUG -pthread

# Then the Theron++ headers and source files are given. Note that the actor 
# file is given as the last source file so that it will be linked as the 
# last file since the other utility classes will be derived from the actor.
# Note also that the communication protocol is not included as it is not 
# generally needed.

THERON_HEADERS = Actor.hpp LinkMessage.hpp SerialMessage.hpp \
		 NetworkLayer.hpp SessionLayer.hpp PresentationLayer.hpp \
	         ConsolePrint.hpp EventHandler.hpp ActorRegistry.hpp
THERON_SOURCE  = $(THERON)/ConsolePrint.cpp \
	         $(THERON)/EventHandler.cpp \
		 $(THERON)/ActorRegistry.cpp \
		 $(THERON)/NetworkEndPoint.cpp \
		 $(THERON)/Actor.cpp
THERON_OBJECTS = ${THERON_SOURCE:.cpp=.o}

# Finally we can form the full set of objective functions for the linker. 
# Currently this is only the set of Theron++ files (which should be combined 
# into a library if more utility classes are added)

ALL_MODULES = $(THERON_OBJECTS)

# Build the individual modules
#
# Compile targets

%.o : %.cpp
	$(CC) $(CFLAGS) $< -o $@ $(INCLUDE_DIRECTORIES)

# The clean target removes all object files to recompile it all

clean:
	${RM} *.o
	$(RM) ${THERON}/*.o
	${RM} *.d
	$(RM) ${THERON}/*.d

#
# TARGETS Linking the modules
#

HelloWorld : HelloWorld.o $(THERON_OBJECTS)
	$(CC) HelloWorld.o $(ALL_MODULES) $(LDFLAGS) -o HelloWorld

ScheduledHelloWorld: Scheduled.o ScheduledHelloWorld.o $(THERON_OBJECTS)
	$(CC) Scheduled.o ScheduledHelloWorld.o $(ALL_MODULES) $(LDFLAGS) -lgmpxx -lgmp -o ScheduledHelloWorld

Serialisation : Serialisation.o $(THERON_OBJECTS)
	$(CC) Serialisation.o $(ALL_MODULES) $(LDFLAGS) -o Serialisation

ZMQDistribution: ZMQDistribution.o $(ZMQ_OBJECTS) $(THERON_OBJECTS)
	$(CC) ZMQDistribution.o $(ZMQ_OBJECTS) $(ALL_MODULES) $(LDFLAGS) $(ZMQ_LIBS) -o ZMQDistribution

#
# DEPENDENCIES
#

-include $(ALL_MODULES:.o=.d)

