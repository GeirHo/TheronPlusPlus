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

  Author: Geir Horn, University of Oslo, 2015
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef THERON_TRANSPARENT_COMMUNICATION
#define THERON_TRANSPARENT_COMMUNICATION 

#include <memory> 		// For shared pointers.
#include <utility>		// For the forward template
#include <map>			// For the managed actors
#include <type_traits>		// For advanced meta-programming

#include <iostream> //TEST

#include <Theron/Theron.h>	// The Theron actor framework

namespace Theron {

class NetworkEndPoint : public EndPoint, public Framework
{
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

    // There is an option to define parameters to be used by the Theron 
    // Endpoint. It returns the default parameters by default.
    
    virtual EndPoint::Parameters SetEndpointParameters( void ) const
    {
      return EndPoint::Parameters();
    }
    
    // It is also possible to set parameters for the Theron Framework, and 
    // again there is a function allowing derived initialisers to change these.
    // In particular it could be useful to change the number of threads used 
    // by Theron to execute actors.
    
    virtual Framework::Parameters SetFrameworkParameters( void ) const
    {
      return Framework::Parameters();
    }

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
  : EndPoint( Name.data(), Location.data(), 
	      TheInitialiser->SetEndpointParameters() ),
    Framework( *this, Name.data(), TheInitialiser->SetFrameworkParameters() ),
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
  
}  	// End namespace Theron
#endif  // THERON_TRANSPARENT_COMMUNICATION