/*=============================================================================
  XMPP Messages

  This file defines the messages used by the XMPP protocol [1]. The transfer
  will be based on the Swiften [2] library. This library has to be built from
  scratch for most operating systems (it is packaged by the developers for
  Debian based OSes). Fedora 31: The build process is straight forward and
  also installs the header files in /usr/local/include.

  References:

  [1] Peter Saint-Andre, Kevin Smith, and Remko Tronçon (2009): “XMPP: The
      Definitive Guide", O’Reilly Media, Sebastopol, CA, USA,
      ISBN 978-0-596-52126-4
  [2] https://swift.im/swiften/

  Author and copyright: Geir Horn, University of Oslo, 2015 - 2020
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef THERON_XMPP_MESSAGES
#define THERON_XMPP_MESSAGES

#include "Swiften/Swiften.h"

// Headers for the Theron++ actor system

#include "Actor.hpp"
#include "Communication/LinkMessage.hpp"
#include "Communication/SerialMessage.hpp"

/*==============================================================================

  External addresses

==============================================================================*/
//
// The Swiften library provides the Jabber ID assuming a symbolic address of
// the form node@domain/resource as a structure. In order for this to be used
// in hashed containers, hashing functions must be provided. Note that the
// return statement calls the hash operator on a temporary string hash object
// (hence the many parentheses)

namespace std {
  template<>
  class hash< Swift::JID >
  {
  public:

    size_t operator() ( const Swift::JID & TheID ) const
    {
      return std::hash< string >()( TheID.toString() );
    }
  };
}

// The same extension must be done for the boost hash function used by
// boost::bimap. Since it is identical to the standard one above, it should
// be possible to reuse that code, but currently I have not found a way to
// do it.

namespace boost {
  template<>
  class hash< Swift::JID >
  {
  public:
    size_t operator() ( const Swift::JID & TheID ) const
    {
      return std::hash< std::string >()( TheID.toString() );
    }
  };
}

// ---------------------------------------------------------------------------
// Jabber ID = external address
// ---------------------------------------------------------------------------
//
// The external address in Theron++ XMPP is the Jabber ID which is simply a
// re-naming of the ID class provided by the Swiften library to allow
// compatibility and reuse of the various definitions for the JID
// JID("node@domain/resource") == JID("node", "domain", "resource")
// JID("node@domain") == JID("node", "domain")

namespace Theron::XMPP
{
	using JabberID = Swift::JID;

/*==============================================================================

  Link messages

==============================================================================*/
//
// The Network Layer will implement the XMPP protocol, and so the link message
// is only used between the Session Layer and the Network Layer to ensure that
// the external IDs are correctly transmitted as Jabber IDs. However, it should
// contain all the information necessary to form the external message, and
// is therefore named accordingly.

class ExternalMessage : virtual public Theron::LinkMessage< JabberID >
{
private:

  // The subject field can be used if the message needs one or is a command
  // between low XMPP layers.

	std::string Subject;

public:

  // The standard constructor takes all the necessary fields and initialises
	// the base class link message accordingly.

  inline ExternalMessage( const JabberID & From, const JabberID & To ,
											    const std::string & ThePayload,
											    const std::string TheSubject = std::string()  )
  : Theron::LinkMessage< JabberID >( From, To, ThePayload ),
    Subject( TheSubject )
  { };

	// The message can also be constructed from a basic link message, with the
	// subject optionally given.

	inline ExternalMessage( const Theron::LinkMessage< JabberID > & BasicMessage,
												  const std::string TheSubject = std::string() )
	: ExternalMessage( BasicMessage.GetSender(), BasicMessage.GetRecipient(),
										 BasicMessage.GetPayload(), TheSubject )
	{ }

  // A copy constructor is necessary to queue messages

  inline ExternalMessage( const ExternalMessage & TheMessage )
  : Theron::LinkMessage< JabberID >( TheMessage ),
    Subject( TheMessage.Subject )
  { };

	// The subject can be defined on an already constructed message if needed,
	// and it can be read from the stored string.

  void SetSubject( const std::string & TheSubject )
  { Subject = TheSubject; }

  virtual std::string GetSubject( void ) const
  { return Subject; }

  // It may also be necessary to recover the actor address from a Jabber ID,
  // and it should be based on the resource string if the address has a
  // resource, otherwise it will be based on the string representation of the
  // whole Jabber ID. Normal actors must have the resource name set to the
  // actor name, and this should be unique to the whole distributed actor
  // system.

  virtual Address ActorAddress( const JabberID & ExternalActor ) const override;

	virtual ~ExternalMessage( void )
	{ }
};

/*==============================================================================

 Serial messages

==============================================================================*/
//
// After the external Jabber ID has been mapped to an actor address, the
// message will be sent from the Session Layer to the receiving actor as
// a Serial message as the receiving actor must be a Deserializing actor
// in order to receive messages. However, when a message is sent from a
// local actor to an actor, the recipient's address is looked up locally, and
// if it does not exist, then the message should be sent to the Presentation
// Layer to be serialized and then forwarded to the Session Layer. There is
// a problem if an application simultaneously uses more than one communication
// protocol since it is not clear which protocol should handle the message,
// i.e. which presentation layer the message must be forwarded to. The
// serial message therefore requires that the message knows this, and defines
// a mandatory function to return the address of the presentation layer.
// This function will just call the static function of the Network Endpoint to
// find this address, so it is not necessary to store the address in each of
// the messages.

class SerialMessage : public Theron::SerialMessage
{
protected:

	virtual Address PresentationLayerAddress( void ) const final;

public:

	SerialMessage( void ) = default;
};

} // End name space Theron XMPP
#endif // THERON_XMPP_MESSAGES
