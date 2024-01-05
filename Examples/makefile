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

THERON = /home/GHo/Documents/Code/Theron++

# Since some of the utility classes still includes "Theron/Theron.h" is it 
# necessary to define this file as a symbolic link to the "Actor.hpp" file of
# Theron++ For the author's installation it is done at the following location

OLD_THERON = ../../Theron/Include

# Optimisation -O3 is the highest level of optimisation and should be used 
# with production code. -Og is the code optimising and offering debugging 
# transparency and should be use while the code is under development

OPTIMISATION_FLAG = -Og

# It is useful to let the compiler generate the dependencies for the various 
# files, and the following will produce .d files that can be included at the 
# end. The -MMD flag is equivalent with -MD, but the latter will include system 
# headers in the output (which we do not need here). The -MP includes an 
# empty rule to create the dependencies so that make would not create any errors 
# if the file name changes.

DEPENDENCY_FLAGS = -MMD -MP

# Options 

GENERAL_OPTIONS = -Wall -std=c++23 -ggdb -D_DEBUG
INCLUDE_DIRECTORIES = -I. -I/usr/include -I$(THERON)

CXXFLAGS = $(GENERAL_OPTIONS) $(INCLUDE_DIRECTORIES) $(DEPENDENCY_FLAGS) \
		   $(OPTIMISATION_FLAG)

# Putting it together as the actual options given to the compiler and the 
# linker. Note that pthread is needed on Linux systems since it seems to 
# be the underlying implementation of std::thread. Note that it is 
# necessary to use the "gold" linker as the standard linker requires 
# the object files in the right order, which is hard to ensure with 
# an archive, and the "gold" linker manages this just fine, but it 
# requires the full static path to the custom Theron library.

CFLAGS = $(DEPENDENCY_FLAGS) $(OPTIMISATION_FLAG) $(GENERAL_OPTIONS)
LDFLAGS = -fuse-ld=gold -ggdb -D_DEBUG -pthread -l$(THERON)/Theron++.a

#------------------------------------------------------------------------------
# Theron library
#------------------------------------------------------------------------------
#
# Then the Theron++ headers and source files are looked up to be used as 
# dependencies for the other targets. 

#THERON_HEADERS := $(shell find $(THEORN) -maxdepth 2 -not \( -path "../Examples" -prune \) -name "*.hpp" -type f -print0 )
#THERON_SOURCE  := $(shell find $(THEORN) -maxdepth 2 -not \( -path "../Examples" -prune \) -name "*.cpp" -type f -print0 )

.PHONY: $(THERON)/Theron++.a 

 $(THERON)/Theron++.a:
	 make -C $(THERON) Library

#------------------------------------------------------------------------------
# The Hello World
#------------------------------------------------------------------------------
#

HelloWorld : $(THERON)/Theron++.a
	$(CC) HelloWorld.cpp $(CXXFLAGS) $(LDFLAGS) -o HelloWorld

#------------------------------------------------------------------------------
# Scheduled
#------------------------------------------------------------------------------
#

ScheduledHelloWorld : $(THERON)/Theron++.a
	$(CC) Scheduled.cpp ScheduledHelloWorld.cpp $(CXXFLAGS) \
	$(LDFLAGS) -o ScheduledHelloWorld

#------------------------------------------------------------------------------
# AMQ Hello World
#------------------------------------------------------------------------------
#

AMQDistribution: AMQDistribution.cpp $(THERON)/Theron++.a
	$(CXX) AMQDistribution.cpp -o AMQDistribution $(CXXFLAGS) \
	-I/home/GHo/Documents/Code/CxxOpts/include $(LDFLAGS) -lqpid-proton-cpp

#------------------------------------------------------------------------------
# AMQ Message test
#------------------------------------------------------------------------------
#

JsonTest: JsonTest.cpp $(THERON)/Theron++.a
	$(CXX) JsonTest.cpp -o JsonTest $(CXXFLAGS) \
	-I/home/GHo/Documents/Code/CxxOpts/include $(LDFLAGS) -lqpid-proton-cpp

#------------------------------------------------------------------------------
# Zero Message test
#------------------------------------------------------------------------------
#
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

ZMQDistribution: ZMQDistribution.cpp $(ZMQ_OBJECTS) $(THERON)/Theron++.a 
	$(CC) ZMQDistribution.o $(ZMQ_OBJECTS) $(LDFLAGS) $(ZMQ_LIBS) -o ZMQDistribution

