/*=============================================================================
  Link Message

  The link message is the format of the message sent on the link, i.e. the
  encapsulation of the payload of the package. The base class defined in
  this file defines the interface functions that must be implemented on this
  message in order to ensure that it minimally contains

  1) The remote receiver address
  2) The sender address
  3) A payload string

  It is defined as a template for the format of the external address. This
  can be a string for IP adresses, but it can be anything that the link supports
  like an integer. However, the address format must be derived from the generic
  external address to ensure that an external address can be encoded by the 
  actor's address and the endpoint's address, where the latter may be given as 
  a string.

  The link message is formatted by the Session Layer and sent by the
  Network Layer. Incoming messages are received by the Network Layer and
  forwarded as a link message object to the Session Layer.

  Author and Copyright: Geir Horn, University of Oslo
  Contact: Geir.Horn@mn.uio.no
  License: LGPL 3.0 (https://www.gnu.org/licenses/lgpl-3.0.en.html)
=============================================================================*/

#ifndef THERON_LINK_MESSAGE
#define THERON_LINK_MESSAGE

// Standard headers

#include <string>                           // Standard strings
#include <concepts>                         // Template concepts

// Boost headers

#include <boost/container_hash/hash.hpp>    // For hashing global addresses

// Theron++ headers

#include "Actor.hpp"                        // The Theron++ actor framework

namespace Theron
{

/*==============================================================================

 Global address
 
==============================================================================*/
//
// The global addresses of actors are supposed to be unique, and the class 
// simply defines the actor identifier and the endpoint identifier as strings
// since it is easy to convert a string to an actor address and vise versa.
// How the actor string and the endpoint string will be combined is protocol 
// dependent.

class GlobalAddress
{
protected:

	const std::string ActorID, EndpointID;

public:

	// Access utility functions

	inline Address ActorAddress( void ) const
	{ return Address( ActorID ); }

	inline std::string ActorName( void ) const
	{ return ActorID; }

	inline std::string Endpoint( void ) const
	{ return EndpointID; }

  // There should be a way to combine the actor identifier and the endpoint 
  // identifier as a single string. How this is done is protocol dependent 
  // and the implementation must be made by a derived address class.
	
	virtual std::string AsString( void ) const = 0;

	inline operator std::string ( void ) const
	{ return AsString(); }

	// Comparison functions based on the string representation

	inline bool operator == ( const GlobalAddress & Other ) const
	{ return (ActorID == Other.ActorID) && (EndpointID == Other.EndpointID); }
	
	inline auto operator <=> ( const GlobalAddress & Other ) const
	{ return AsString() <=> Other.AsString(); }
	
	// Constructors.
	
	inline
	GlobalAddress( const Address & TheActor, const Address & TheEndpoint )
	: ActorID( TheActor.AsString() ), EndpointID( TheEndpoint.AsString() )
	{}

	inline
	GlobalAddress( const Address & TheActor, const std::string & TheEndpoint )
	: ActorID( TheActor.AsString() ), EndpointID( TheEndpoint )
	{}

	inline
	GlobalAddress( const std::string & TheActor, const std::string & TheEndpoint )
	: ActorID( TheActor ), EndpointID( TheEndpoint )
	{}

	inline GlobalAddress( const GlobalAddress & Other )
	: ActorID( Other.ActorID ), EndpointID( Other.EndpointID )
	{}

	GlobalAddress( void ) = delete;
	virtual ~GlobalAddress( void ) noexcept = default;
};

  
/*==============================================================================

 Link Message

==============================================================================*/
//
// The link message is defined on an external address that must be derived from
// the address above, and a message payload. It basically stores and provides 
// controlled access to the the three elements given: The global address of 
// the sender, the global address of the receiver, and the message payload.
// Both the address type and the message payload must therefore be copy
// constructible meaning that the class can be initialised from the values 
// provided to the the constructor. The link message is used for the exchange 
// of messages between the Network Layer Actor and the Session Layer Actor.

template < class ExternalAddress, 
           class MessagePayload >
requires std::derived_from< ExternalAddress, GlobalAddress > &&
		     std::copy_constructible< ExternalAddress > &&
		     std::copy_constructible< MessagePayload >
class LinkMessage
{
protected:

	// The message is only valid if it has a sender, a receiver and a payload
	// set. Hence these are parameters to the constructor, and read only fields
	// for derived messages.

	const ExternalAddress  SenderAddress, ReceiverAddress;
	const MessagePayload   Payload;

public:

  using AddressType = ExternalAddress;
  using PayloadType = MessagePayload;

  // One should be able to obtain the payload string of the message.

  inline MessagePayload GetPayload( void ) const
  { return Payload; }

  // There are similar functions to obtain or set the sender and the receiver of
  // this message.

  inline ExternalAddress GetSender( void ) const
  { return SenderAddress; }

  inline ExternalAddress GetRecipient( void ) const
  { return ReceiverAddress; }
  
  // The standard constructor takes the Sender address, the Receiver address
  // and the payload and initialises the various fields accordingly.

  LinkMessage( const ExternalAddress & From, const ExternalAddress & To,
			         const MessagePayload ThePayload )
  : SenderAddress( From ), ReceiverAddress( To ), Payload( ThePayload )
  { }

  // A message can also be copied from another message, in which case it
  // simply delegates the initialisation to the standard constructor.

  LinkMessage( const LinkMessage & Other )
  : LinkMessage( Other.SenderAddress, Other.ReceiverAddress, Other.Payload )
  { }

  // The default constructor is explicitly deleted as it should not be used.

  LinkMessage() = delete;

  // The destructor is virtual to support correct deallocation of derived
  // classes, but it does not do anything in particular.

  virtual ~LinkMessage() noexcept = default;
};

// -----------------------------------------------------------------------------
// Testing for valid external messages
// -----------------------------------------------------------------------------
// 
// The the validity of a type as a valid message is more involved because the 
// template parameters of the link message must be tested but based on the 
// message type. The idea is first to test that the type defines the address 
// type and the payload type. The address type must be derived from the 
// global address above, and then the message type itself must be derived from 
// the link message based on the given address type and the payload type.

template< class MessageType >
concept ValidLinkMessage = requires
{
	typename MessageType::AddressType;
	typename MessageType::PayloadType;
	requires std::derived_from< typename MessageType::AddressType, GlobalAddress >;
	requires std::derived_from< MessageType, 
					LinkMessage< typename MessageType::AddressType, 
											 typename MessageType::PayloadType > >;
};

};// name space Theron

/*==============================================================================

  Hashing global addresses

==============================================================================*/
//
// This global address will be used in unordered maps that are based on the hash
// functions for the string representation of the address. These hash functions
// must belong to the std name space for the standard maps, and to the boost
// name space for the bi-map used by the session layer. 

// Specialisation for the standard map hash function

namespace std {
  template<>
  class hash< Theron::GlobalAddress >
  {
  public:

    size_t operator() ( const Theron::GlobalAddress & TheID ) const
    { return std::hash< std::string >()( TheID.AsString() ); }
  };
} // end name space std

// Specialisation for the boost bi-maps

namespace boost {
  template<>
  class hash< Theron::GlobalAddress >
  {
  public:

    size_t operator() ( const Theron::GlobalAddress & TheID ) const
    { return std::hash< std::string >()( TheID.AsString() ); }
  };
} // end name space boost

#endif 	// THERON_LINK_MESSAGE
