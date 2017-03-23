/*=============================================================================
  Network End Point
  
  The original communication architecture of Theron has an End Point class that
  deals with the external input and output activities. In addition there is a 
  Framework class dealing with all the actors and the message passing inside 
  the network endpoint. In order to allow node external communication, the 
  Framework has to be created (bound) to the the End Point object.
  
  With the aim of creating transparent communication these two classes should 
  fundamentally merge into the same mechanism hosting actors and taking care 
  of their communication. This is the purpose of this class. 
  
  It encompasses the Network Layer Server and the Session Layer Server to 
  ensure that they are confirming to the communication protocol of the 
  actor system.

  Author: Geir Horn, University of Oslo, 2015-2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef THERON_TRANSPARENT_COMMUNICATION
#define THERON_TRANSPARENT_COMMUNICATION 

#include <memory> 					// For shared pointers.
#include <utility>					// For the forward template
#include <map>							// For the managed actors
#include <set>							// For shut down management
#include <type_traits>			// For advanced meta-programming
#include <initializer_list> // For structured initialisation

#include <iostream> 				//TEST

#include <Theron/Theron.h>	// The Theron actor framework

namespace Theron {

class NetworkEndPoint : public EndPoint, public Framework
{
  // ---------------------------------------------------------------------------
  // Domain framework
  // ---------------------------------------------------------------------------

private:
  
  // The parameters provided describing the location of this end point is 
  // stored for future reference by communication specific derived classes
  
  std::string  Domain;
  
public:
  
  // Functions to access the information about the endpoint to make this 
  // read-only
  
  inline std::string GetDomain( void ) const
  {
    return Domain;
  }
  
  inline Framework & GetFramework( void ) 
  {
    return *this;
  }
  
  // ---------------------------------------------------------------------------
  // Framework parameters
  // ---------------------------------------------------------------------------
  // The Theron framework parameters is provided in a C-like fashion, and not
  // changeable after the Framework creation - although there are functions to 
  // express change goals. To improve the interface to these, there is a 
  // static object that allows to set the framework parameters prior to the 
  // creation of the Network End Point. The address of this class is passed 
  // to the Framework's constructor
  //
  // Currently the end point parameters are empty, but included for future 
  // expansion. 
  
  static class FrameworkParameters : protected EndPoint::Parameters,
																		 protected Framework::Parameters
  {
	public:
		
		// Yield types are encapsulated to force them to be explicit. The 
		// conditional strategy means that inactive threads are suspended making it
		// somewhat slower to restart a worker thread, but at the benefit of not 
		// consuming any power when the threads are mostly idle. The spin strategy 
		// is probably better for real-time response systems since it keeps all 
		// worker threads active at all times, even when they will not be used by
		// any actor. The hybrid strategy is a compromise which tries to avoid the 
		// rampant consumption of spare CPU cycles caused by busy-waiting, while 
		// still avoiding condition variables, by yielding to other threads with a 
		// time out after some period of spinning.
		
		enum class YieldType
		{
			Conditional = YIELD_STRATEGY_CONDITION,
			Spin		    = YIELD_STRATEGY_SPIN,
			Hybrid			= YIELD_STRATEGY_HYBRID
		};
		
		// Thread related functions. The first sets the number of worker threads 
		// to use to execute the actors of the framework. The second sets the 
		// yield strategy. There is also a parameter for thread priority, but 
		// the effect of this is undocumented and it is therefore not provided.
		
		inline void WorkerThreadCount( const uint32_t Count )
		{
			mThreadCount = Count;
		}
		
		inline void Yield( const YieldType Strategy )
		{
			mYieldStrategy = static_cast< YieldStrategy >( Strategy );
		}
		
		// Non-Uniform Memory Architecture (NUMA) is a cluster computer architecture
		// where each node has its own set of processors and each node has its
		// own memory, and each processor has its own cache. Thus one can gain 
		// significant performance by allocating the worker threads to the same 
		// nodes and the same processors. Theron provides experimental support for 
		// such restrictions by masks for nodes and processors.
		
		inline void NUMANodeMask( const uint32_t Mask )
		{
			mNodeMask = Mask;
		}
		
		inline void NUMAProcessorMask( const uint32_t Mask )
		{
			mProcessorMask = Mask;
		}
		
		// The constructor is simply a place holder to ensure that the default 
		// values are used for the endpoint and framework parameter classes.
		
		FrameworkParameters( void )
		: EndPoint::Parameters(), Framework::Parameters()
		{ }
		
		// In order to allow the constructor to pass the base classes to the 
		// end point and the framework, access to the protected parameter classes
		// must be allowed by the Network End Point constructor.
		
		friend class NetworkEndPoint;
		
	} Parameters;
  
  // ---------------------------------------------------------------------------
  // Actors: Network Endpoint Layers
  // ---------------------------------------------------------------------------
  // The core functionality is to encapsulate the necessary communication 
  // actors: The Network Layer server dealing with the physical link exchange 
  // and sockets; the Session Layer taking care of the the mapping of external
  // actor addresses and actor addresses on this framework; and the Presentation
  // layer server allowing actors to send messages transparently to actors on 
  // remote endpoints. These unique layers are defined according to the OSI 
  // model.
  
  enum class Layer 
  {
    Network,
    Session,
    Presentation
  };
    
private:
  
  // Independent on how the servers are implemented, we know that they must be 
  // actors, and hence we can keep a pointer to them as actors. Note that the 
  // pointer must be shared to allow the access to these pointers from derived 
  // classes (unique_ptr cannot be used as they cannot be copied).
  //
  // Since all operations on these actors will be similar, there is no need 
  // to implement dedicated functions for each of the three actors, e.g. to 
  // obtain the Theron Address of the actor. They can be accessed by the 
  // function they have, and the pointers are therefore kept in a map.
  
  std::map< Layer, std::shared_ptr< Actor > > CommunicationActor;
  
  // The actual communication actors are depending on the protocol, and in 
  // order to access functions on these classes, the pointers must be cast 
  // to the pointers of the right class type. This is the purpose of the 
  // following access function. It would be possible, but very complicated
  // to just pass the class and the function to be called on the class to a 
  // caller function (which would be necessary had the pointers been unique 
  // pointers) to ensure that the pointers could not be accidentally deleted 
  // by the caller code.

public:
  
  template< class ServerClass >
  std::shared_ptr< ServerClass > Pointer( Layer Role )
  {
    std::shared_ptr< ServerClass > ThePointer = 
	  std::dynamic_pointer_cast< ServerClass >( CommunicationActor[Role] );
	  
    if ( ThePointer )
      return ThePointer;
    else
      throw std::logic_error("Network server inheritance error");
  }
  
  // It is the responsibility of the constructor of the communication specific 
  // derived class to ensure that these actors are created by calling the 
  // following functions with the correct class type. The first argument to 
  // the constructor of these actors should be a pointer to this Network 
  // End Point that can be understood as an endpoint or a framework as 
  // necessary. The other parameters must be given to the creator function 
  // below. The creator method takes the function of the actor to be created 
  // as first argument, and then the other arguments are forwarded to the 
  // actor's constructor.
  //
  // The arguments are passed using the forwarding mechanism to be able to 
  // handle all kind of different arguments, see the excellent answer at
  // http://stackoverflow.com/questions/3582001/advantages-of-using-forward
  // for details. Note that when unrolling the argument list the forward 
  // template is iterated over all arguments (types and values) This iteration
  // taken from http://eli.thegreenplace.net/2014/variadic-templates-in-c/

  template< class ServerClass, typename ... ArgumentTypes >
  inline void Create( Layer Role, ArgumentTypes && ... ConstructorArguments )
  {
    static_assert( std::is_base_of< Actor, ServerClass >::value,
    "Network End Point: Network servers must be Actors!");
    
    CommunicationActor[Role] = std::shared_ptr< Actor >( 
      new ServerClass( this, 
	  std::forward< ArgumentTypes >(ConstructorArguments)... 
      ));
  }

  // In some cases it can be necessary to directly address these actors,
  // for extended initialisation messages to provided handlers. The following 
  // access function returns the actor address provided that the 
  // corresponding actor has been created.
  
  inline Address GetAddress( Layer Role )
  {
    std::shared_ptr< Actor > TheServer = CommunicationActor[Role];
    
    if ( TheServer )
      return TheServer->GetAddress();
    else
      return Theron::Address::Null();
  }
  
  // There is also a function to check if an address exists at the local 
  // endpoint. This is typically used when a message arrives from a remote 
  // endpoint searching for the endpoint knowing a particular actor. It will
  // use the EndPoint's lookup function to check if the provided address 
  // corresponds to a local actor. The function requires a message queue 
  // index, which will be non-zero if the queue for this actor exists at this
  // endpoint (node), but it is simply ignored since the actor presence is 
  // indicated by the boolean return value of the lookup function.
  
  bool IsLocalActor( const Address & RequestedActorID )
  {
    Detail::Index MessageQueue;
    return Lookup( Detail::String( RequestedActorID.AsString() ), 
		   MessageQueue );
  }
  
  // ---------------------------------------------------------------------------
  // Shut down management
  // ---------------------------------------------------------------------------
  // A small problem in Theron is that there is no function to wait for 
  // termination. Each actor is in principle a state machine that will run 
  // forever exchanging messages. The thread that starts main and creates the 
  // network endpoint and the actors should be suspended until the actor system 
  // itself decides to terminate. The correct way to do this is to use a 
  // Receiver object that will get a last message when all the key actors in 
  // the system no longer have messages to process. Thus when this receiver 
  // terminates its wait, the main() will probably terminate calling the 
  // destroying the objects created within main(). 
  //
  // In larger systems there will be more actors than threads and, perhaps, more 
  // threads than cores. The result is that some threads may be waiting for an 
  // available core and some actors may be waiting for available threads. Thus,
  // when the deciding actor has entered the finishing state, there may still 
  // be unhanded messages for other actors, and some of these may be processed 
  // in other threads while main() is destroying the actors almost guaranteeing 
  // that the application will terminate with a segmentation fault. 
  //
  // Given that there is no active interface to the Theron scheduler, it is not
  // possible to know when there are no pending messages. However, if the actors
  // created in main() are known, then it is possible to verify that all their
  // message queues are empty before terminating. 
  //
  // This idea is implemented with a Receiver that has a set of pointers to 
  // actors to wait for. When it receives the shut down message it will loop 
  // over the list of actors checking if they have pending messages, and if so
  // it will suspend execution for one second. The handler will only terminate 
  // when there are no pending messages for any of the guarded actors. 
  // 
  // There are two methods provided by the Network End Point to use this 
  // Receiver: One Wait For Termination that creates the receiver and waits 
  // for it to receive the shut down message, and one Shut Down method that 
  // can be called by the main actor when the system is ready for shut down
  // as the message exchange dries up.
  
public:
	
	class ShutDownMessage
	{
	private:
		
		std::string Description;
		
	public:
		
		ShutDownMessage( void )
		{
			Description = "Network End Point: Shut down message";
		}
		
	};
	
  // The receiver object has a handler for this message
	
private:
	
	class TerminationReceiver : public Receiver
	{
	private:
		
		std::set< const Actor * > GuardedActors;
		
		// The termination message handler
		
		void StartTermination( const ShutDownMessage & TheRequest, 
													 const Address Sender );
		
	public:
		
		// The constructor takes an initialisation list to ensure that all pointers
		// are for actors.
		
		TerminationReceiver( 
									const std::initializer_list< const Actor * > & ActorsToGuard )
		: GuardedActors( ActorsToGuard )
		{
			RegisterHandler( this, &TerminationReceiver::StartTermination );
		}
		
	};
  
	// There is a shared pointer for this termination receiver to ensure that it 
	// is properly destroyed and so that it can be passed outside if someone 
	// wants to manage the sending of the termination message directly.
	
	std::shared_ptr< TerminationReceiver > ShutDownReceiver;
	
	// There is a public interface function to send the shut down message to this
	// receiver. It optionally takes the address of the calling actor and sends 
	// the message as if it was from that actor. It throws if there is no shut 
	// down receiver allocated.
	
public:
	
	void SendShutDownMessage( const Address & Sender );
	
	// There is also a variant that will send the message as if it is from the 
	// network layer actor.
	
	void SendShutDownMessage( void )
	{
		SendShutDownMessage( GetAddress( Layer::Network ) );
	}
	
	// One may get the address of the shut down receiver. This function will 
	// also throw if the receiver does not exist.
	
	Address ShutDownAddress( void );
	
	// The shut down receiver and object is created and obtained by a creator 
	// function that takes a list of actors that can be converted to actor 
	// pointers and passed to the termination receiver. Note that it adds the 
	// network layer servers to the set of actors to look after. 
	// 
	// Note: There is deliberately no Wait function as this has to be called 
	// on the object returned by this function, i.e. by invoking the standard 
	// wait function on the receiver.
	//
	// Implementation note: The initialiser list must be given as an initialiser 
	// in stead of the constructor, and the compiler will call the correct 
	// constructor. In other words, if the initialiser list constructor should 
	// be called, one would need to say T{...} and not T({...}), and the 
	// standard make shared will implicitly use the latter case. The alternative 
	// could be to template the constructor, but then it would not be as easy to
	// ensure that the types of the given arguments were really pointers to actors
	
	template< class... ActorTypes >
	std::shared_ptr< TerminationReceiver > 
	TerminationWatch( ActorTypes & ...TheActor )
	{
		ShutDownReceiver = std::shared_ptr< TerminationReceiver >(
			new TerminationReceiver
				  { CommunicationActor[ Layer::Network 			].get(), 
						CommunicationActor[ Layer::Session 			].get(), 
						CommunicationActor[ Layer::Presentation ].get(),
						dynamic_cast< const Actor * >( &TheActor )...
					}
		);
			
		return ShutDownReceiver;
	}
	
  // ---------------------------------------------------------------------------
  // Constructor and Initialiser
  // ---------------------------------------------------------------------------
  // The constructor simply stores the arguments and ensures consistency between
  // the endpoint and the framework. First the arguments for the end point are
  // given and then the arguments for the framework are accepted. It would be 
  // tempting to initialise the various network layer actors from the 
  // constructor, but this creates a fundamental problem: Imagine the situation
  // where a class is derived from this class and then a second class is 
  // derived from that class again:
  //
  // class DerivedFirst : public NetworkEndPoint
  // class DerivedSecond : public DerivedFirst
  // 
  // DerivedFirst may not know about DerivedSecond, and create the types of 
  // actors it has been designed for. DerivedSecond will also create the actors
  // it has been designed for, and this will be at best inefficient since it 
  // deletes the actors created by derived first.
  //
  // It does not help creating virtual functions for creating these actors! 
  // A virtual function is a placeholder for another function's address, and 
  // even if DerivedSecond and DerivedFirst defines virtual functions, the 
  // DerivedSecond functions are not initialised before its constructor executes
  // (because DerivedFirst should obviously be assured to call its own
  // virtual functions)
  //
  // It is also not possible to pass a pointer to an initialisation function 
  // because this function must be a derived class member in order to access 
  // the Create function, and it needs to know the parameters to pass to the 
  // constructor of the communication layer server it creates.
  //
  // The way to circumvent this problem is combining some of the very nice 
  // features of C++: We define an initialiser object with a virtual 
  // function to initialise the  communication layer classes. This class must 
  // be a friend of the derived classes so that the initialiser can access 
  // the create function of the Network End Point (this class). Then this 
  // object is taken as a template argument to the constructor, and derived 
  // classes can have default values for this initialiser. This initialiser 
  // is then instantiated by the base class constructor with the base class
  // as argument, and then the initialisation function is called. 
  //
  // There is an issue that the initialiser class will use the create function
  // and the pointer function above from a base class pointer, but since the 
  // initialisers will embedded classes in a derived class they have no access
  // to protected members of the Network End Point, and the classes cannot be 
  // declared friends of the base class. Hence, the only solution is to make
  // the Create and Pointer functions above public members.
  
protected:
  
  friend class Initialiser;
  
  class Initialiser
  {
  private:
      
    // Access to the functions of the base class is done through this pointer
    // since this object's this pointer is not very useful
    
    NetworkEndPoint * TheNode;

    // There is fundamental problem of ordering. The functions creating the 
    // servers and binding them can only be called from the base network 
    // endpoint class' constructor. Hence this can be ensured by only
    // instantiating the class in the the base endpoint class' constructor. 
    // However, the functions to set the default parameters for the Theron 
    // Endpoint and Framework base classes of the Network End Point base class
    // must be invoked before the base endpoint class constructor is executed.
    // If base classes should be allowed to re-define these, an object must 
    // be created prior to invoking the any of these functions for the normal 
    // polymorphism to work. Hence the initialiser class must be constructed
    // before the base endpoint class, and it can therefore not take the pointer
    // to the base endpoint class as a parameter to the constructor. The base
    // class is therefore declared as a friend in order to set the node pointer

    friend class NetworkEndPoint;
    
  protected:
    
    // There is an interface function allowing a derived initialiser to use 
    // the node pointer, essentially giving read only access.
    
    inline NetworkEndPoint * GetNodePointer( void )
    {
      return TheNode;
    }
    
  public:
    
    // The initialisation is split into two parts: The first is to create the 
    // classes implementing the various networking interface layers. This is 
    // very much protocol dependent, and other classes deriving from this 
    // network endpoint must provide a function to create the classes using 
    // the templated creator function found in the network end point class.
    
    virtual void CreateServerActors ( void ) = 0;
    
    // After the creation of the classes, they will be bound together. The 
    // default implementation is to bind
    // 		Network Layer <-> Session Layer <-> Presentation Layer
    // but any kind of binding can be offered using any methods defined for 
    // the actors serving the different layers.
    
    virtual void BindServerActors ( void ) = 0;

    // The constructor simply ensures that we capture a true base class pointer
    // when this initialiser is instantiated by the base class. If any of the 
    // virtual functions attempts to dynamically cast this pointer to anything
    // else, an exception should be thrown.
    
    Initialiser( void )
    {
      TheNode = nullptr;
    }
    
    // There is also a virtual destructor doing nothing, but important for the 
    // correct inheritance.
    
    virtual ~Initialiser( void )
    { }
  };
  
public:
  
  // The constructor could then templated on the the initialiser class which is 
  // instantiated in the constructor. This ensures that only the base class 
  // initialises the communication classes (unless some derived class does it
  // explicitly). 
  //
  // There is one hatch though: Even though a constructor can be templated, 
  // the template parameters are supposed to be for the types of the constructor
  // arguments, and not to be used explicitly. In other words, one can cannot 
  // make a derived constructor like:
  // 	Derived( ... ) : Base< A >(....)
  // even though the base class constructor is a template. However, it is 
  // possible to define the base constructor as
  // 	template < A > Base( A parameter ) 
  // and then the 'parameter' can be of any useful type. 
  // 
  // The consequence of this is that we need to pass the initialiser class as
  // a constructor argument type, but we should not allow this argument to do
  // anything useful. There is a good proposed solution in 
  // http://stackoverflow.com/questions/2786946/c-invoke-explicit-template-constructor
  // by first defining a dedicated class that does nothing but defining its
  // type (and this should never be used)
  //
  // Furthermore, there is an issue with some compilers that they do not accept
  // variadic template arguments after default values have been defined for 
  // some arguments because default values should always come at the end. To 
  // remedy this, a template function is used to create the initialiser taking
  // optional arguments that may be needed by derived initialisers. It returns
  // a shared pointer so that the object is deallocated as soon as the 
  // initialisation is over, and this is a type defined first.
  
  typedef std::shared_ptr< Initialiser > InitialiserType;
  
  // Then the creator function can be defined. It is defined as static as it 
  // does not require a 'this' pointer since it does not access any class 
  // members, and declaring it static will allow it to be used without first 
  // creating the network end point (which would defeat the purpose)
  
  template< class InitialiserClass, typename ... InitialiserArgumentTypes >
  static InitialiserType SetInitialiser( 
			  InitialiserArgumentTypes && ... InitialiserArugments )
  {
    static_assert( std::is_base_of< Initialiser, InitialiserClass >::value,
    "Network End Point: The initialiser must be derived from class Initialiser");

    return InitialiserType( new InitialiserClass( 
    std::forward< InitialiserArgumentTypes >( InitialiserArugments )... )   );
  }

 // The constructor takes the right initialiser and uses it first to set the 
 // parameters for the Theron Endpoint and Framework, before it is bound to 
 // this endpoint and the network server classes are created and bound. 
 // 
 // A minor implementation note: The initialiser cannot be a constant since 
 // the constructor will set the node pointer to this (i.e. change the object)
 // and that means it cannot be a reference to a temporary object, but must be
 // a copy. However, what is copied is the shared pointer, not the initialiser,
 // and therefore the overhead should be negligible. 
  
 NetworkEndPoint( const std::string & Name, const std::string & Location,
								  InitialiserType TheInitialiser )
  : EndPoint( Name.data(), Location.data(), Parameters),
    Framework( *this, Name.data(), Parameters ),
    Domain( Location ), CommunicationActor()
  {
    
    TheInitialiser->TheNode = this;
    
    TheInitialiser->CreateServerActors();
    TheInitialiser->BindServerActors();
  }
  
  // The destructor is also not doing anything particular since the managed 
  // actor will be destroyed by the unique pointer destructor
  
  virtual ~NetworkEndPoint( void )
  { }
};
  
}  			// End namespace Theron
#endif  // THERON_TRANSPARENT_COMMUNICATION
