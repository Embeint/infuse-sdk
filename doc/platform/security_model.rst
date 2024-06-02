.. platform-security-model:

Security Model
##############

The Infuse IoT security model has been designed to enable the platform
:ref:`platform-design-goals` while maintaining confidentiality and authenticity
of data to the greatest extent possible.

The Infuse platform splits its communication security into two authentication
levels, both integrated on top of the :ref:`epacket_api` API.

Common Parameters
*****************

Algorithms
----------

The Infuse platforms default payload encryption protocol is
`ChaCha20-Poly1305`_, an `Authenticated-Encryption Associated-Data`_ (AEAD)
algorithm that is more performant than alternative like AES-GCM while also
being supported by industry standard cryptography libraries like `MbedTLS`_
and `PSA`_.

Asymmetric key agreement is perfomed through the Elliptic-Curve Diffie-Hellman
(`ECDH`_) protocol. The selected curve is `Curve25519`_, which is well
supported, more efficient than the standard prime curves (P-256, etc), and free
from any doubt about standards interference. Curve25519 also has a more compact
public key representation, 32 bytes instead of 65 bytes for P-256.

Key derivation is performed by the `HKDF`_ algorithm.

.. note::

    Faster algorithms than `ChaCha20-Poly1305`_ exist, like the NIST LWC
    competition winner `ASCON`_. If and when these algorithms make it into
    industry standard libraries we will consider a transition.

Indirect usage of keys
----------------------

The root shared secret between communications partipants is never used to
directly encrypt data. This limits the impact of any key recovery attack
against transmitted data, as recovering the data encryption key does not
lead to the compromise of the root shared secret. For interfaces where the
derived encryption key rotates automatically with time, this also limits
the duration over which a recovered key can usefully attack the network.

.. graphviz::
   :caption: Key Derivation

    digraph {
        node [shape=box];

        subgraph cluster_device {
            label = "Device Encryption";

            CLOUD_PAIR [label="Global Infuse Cloud\nECC Curve25519 Key Pair"];
            DEVICE_PAIR [label="Device Generated\nECC Curve25519 Key Pair"];
            ROOT_KEY [label="Root key"];
            DEVICE_UDP_KEY [label="UDP Key"];
            DEVICE_SERIAL_KEY [label="Serial Key"];
            DEVICE_BLUETOOTH_KEY [label="Bluetooth Key"];
            DEVICE_SIGN_KEY [label="Sign Key"];
            DEVICE_UDP_PKT [label="UDP Packet"];
            DEVICE_SERIAL_PKT [label="Serial Packet"];
            DEVICE_BLUETOOTH_PKT [label="Bluetooth Packet"];

            CLOUD_PAIR -> ROOT_KEY [label="ECDH(Device Public Key)"]
            DEVICE_PAIR -> ROOT_KEY [label="ECDH(Cloud Public Key)"]
            ROOT_KEY -> DEVICE_UDP_KEY [label="HKDF('udp...'\)"]
            ROOT_KEY -> DEVICE_SERIAL_KEY [label="HKDF('serial...')"]
            ROOT_KEY -> DEVICE_BLUETOOTH_KEY [label="HKDF('bt...')"]
            ROOT_KEY -> DEVICE_SIGN_KEY [label="HKDF('sign...')"]
            DEVICE_UDP_KEY -> DEVICE_UDP_PKT [label="CHACHA20-POLY1305"]
            DEVICE_SERIAL_KEY -> DEVICE_SERIAL_PKT [label="CHACHA20-POLY1305"]
            DEVICE_BLUETOOTH_KEY -> DEVICE_BLUETOOTH_PKT [label="CHACHA20-POLY1305"]
        }

        subgraph cluster_network {
            label = "Network Encryption";

            NETWORK_SECRET [label="Network Root key"];
            NETWORK_SERIAL_KEY [label="Serial Key"];
            NETWORK_BLUETOOTH_KEY [label="Bluetooth Key"];
            NETWORK_SERIAL_PKT [label="Serial Packet"];
            NETWORK_BLUETOOTH_PKT [label="Bluetooth Packet"];
            NETWORK_SECRET -> NETWORK_SERIAL_KEY [label="HKDF('serial...' + time)"]
            NETWORK_SECRET -> NETWORK_BLUETOOTH_KEY [label="HKDF('bt...' + time)"]
            NETWORK_SERIAL_KEY -> NETWORK_SERIAL_PKT [label="CHACHA20-POLY1305"]
            NETWORK_BLUETOOTH_KEY -> NETWORK_BLUETOOTH_PKT [label="CHACHA20-POLY1305"]
        }

        DEVICE_SIGN_KEY -> NETWORK_SERIAL_PKT [label="MAC"]
        DEVICE_SIGN_KEY -> NETWORK_BLUETOOTH_PKT [label="MAC"]
    }

Key Rotation
------------

All keys used for encrypted communications can be rotated at any time after
system deployment. Key rotation may incur a communication overhead for either
participant to recompute the shared secret. Key rotation may require an
over-the-air upgrade of the device firmware, depending on the key to be
rotated.

Key Identification
------------------

The key used to encrypt packets should be identifiable from the packet
contents. This enables devices to skip the expensive process of attempting
to decrypt a packet if it cannot succeed.

.. note::

    Identifying the key is different from transmitting the key. The key
    identity is either a numeric identifier for networks or a public key
    hash for devices.

Network Encryption
******************

Network encryption is used if the data to be transmitted is intended for
use in a local context, i.e. without cloud involvement. The most common use
case for this is Bluetooth advertising broadcast, but also includes Bluetooth
GATT communications between two devices.

Confidentiality
---------------

Each network shares a single common root key, present on every device in the
network. The network root key is used to derive a network key for each
communications interface, which is valid for some duration of time.

The primary risk of network encryption as implemented is that a hardware
attack against any device in the network (ROM readback, decapping, etc) can
compromise the security of the entire network. For this reason, network
authenticated packets should only be used for informative purposes. They should
not be able to reconfigure the device, trigger destructive actions, or any
other similar action.

To mitigate the cost of recovering from such an attack, the root network key
can be swapped out with a single device authenticated RPC at any time.

This vulnerability cannot be eliminated without violating the platform
:ref:`platform-design-goals`. Primarily those of
:ref:`platform-design-goals-intermittent-comms` and
:ref:`platform-design-goals-d2d-comms`. In order for devices to broadcast
encryption information to each other without access to some external source of
truth, the only option is for each device to share some common secret.

Authenticity
------------

While data encrypted to the network level is encrypted with a shared common
key, the message as a whole can still be signed with a MAC based on a private
device key. This enables the Infuse cloud to validate the authenticity of the
sending device despite the shared encryption key.

Individual devices cannot valiate the MAC of received packets however, as they
do not have access to the cloud private keys that are used to derive the remote
devices signing key.

.. warning::

    The MAC associated with a message may be truncated from its ideal size
    depending on the interface due to payload size considerations. If
    truncated, the authenticity guarantees are neccesarily weakened,
    potentially to the point that collision attacks are feasible. This is
    considered an acceptable tradeoff given the reduced sensitivity of any
    data encrypted to the network level.

Key Rotation
------------

Key rotation is achieved via setting a new root secret and network identifier
from the cloud.

Device Encryption
*****************

Device encryption is used for data intended for a specific device, generated
by an authorised user. Device encryption is required for any commands that
reconfigure the device, or have potential harmful side effects.

Confidentiality
---------------

Data encrypted at the device level uses a symmetric key established using the
standard `ECDH`_ protocol. The symmetric key is therefore never transmitted
over the network or stored in ROM, limiting the opportunity for any attacker to
recover the key.

Authenticity
------------

Devices can be sure of the cloud servers authenticity through the use of a
static public key, as any data that successfully decrypts was generated by
an entity with access to the corresponding private key.

Devices are expected to know the cloud public key, which is static, while the
cloud is expected to dynamically query the device public key when it detects
a change or observes the device for the first time.

Ensuring the authenticity of devices is more complicated, as anyone can
generate a valid shared secret under ECDH. Without access to a certificate
authority (see :ref:`platform-design-goals-non-ip`), an alternate validation
scheme must be used. Because the device public key must be known by the cloud
in order to generate the shared secret, this provides a natural time to perform
identity verification.

In the same response that contains the device public key, the device must also
provide some piece of information that Infuse cloud can use to validate that
the key is being reported by the expected device.

Newer Nordic SoCs can make use of the `Nordic Identity Service`_ to do this, as
they contain a permanent ECC key pair which can be used to validate device
identity. Other SoCs must be explicitly provided with a secret value by Infuse
cloud at the point of :ref:`platform-provisioning`. This value can then be
encrypted with the proposed shared secret (to prevent exposing the value) and
provided to Infuse cloud as proof of identity. Compromise of the value would
allow the device to be impersonated, but would not allow decryption of the
real devices communications.

.. msc::

    Device,Cloud;

    Device=>Cloud [ label = "Network encrypted data HASH(KP1)" ];
    ...;
    Device=>Cloud [ label = "Network encrypted data HASH(KP1)" ];
    Device=>Device [ label = "Regenerate ECC key pair (KP2)" ];
    Device=>Cloud [ label = "Network encrypted data HASH(KP2)" ];
    Cloud=>Cloud [ label = "Detect key pair change" ];
    Cloud=>Device [ label = "Query public key (Challenge bytes)"];
    --- [ label = "Standard SoC challenge response" ];
    Device=>Device [ label = "Compute challenge response\nPublic Keys + ENCR(Challenge + Secret + ID)"];
    Device=>Cloud [ label = "Key info (Challenge Response)" ];
    Cloud=>Cloud [ label = "Validate challenge response" ];
    --- [ label = "Nordic Identity Service response" ];
    Device=>Device [ label = "Compute challenge response\nGenerate identity attestation"];
    Device=>Cloud [ label = "Key info (Attestation token)" ];
    Cloud=>Cloud [ label = "Validate attestation token with nRF Cloud" ];

.. note::

    By definition the device public key is not a confidential secret. Cloud
    services can therefore query the public key and perform identity validation
    at the network encryption level.

Key Rotation
------------

As per standard ECDH, the symmetric key is the product of the local public +
private key and the remote public key. There are 3 potential components that
can be compromised.

* Shared symmetric key
* Device private key
* Cloud private key

If the shared symmetric key or device private key are suspected to be
compromised, the device can regenerate a new EC key-pair. This will
automatically change the shared symmetric key in an unpredicatable fashion,
securing future communications. If rotated autonomously (without cloud
involvement), the cloud can automatically determine the key-pair has changed
through observing the public key hash that is part of the message header. All
that is required to re-established secure communications is to query the public
key of the device.

If the cloud private key is suspected or known to be compromised, the devices
knowledge of the cloud public key must be updated in order to re-establish
secure communications. This can be done temporarily through the
:ref:`kv_store_api`, with a more permanent value change triggered through an
OTA update.

.. note::

    Compromise of the cloud private key is a worst-case scenario that is not
    expected to occur in practice. If it does occur, all customers will be
    immediately contacted with additional details through all possible methods.

.. _Authenticated-Encryption Associated-Data: https://en.wikipedia.org/wiki/Authenticated_encryption
.. _HKDF: https://en.wikipedia.org/wiki/HKDF
.. _ChaCha20-Poly1305: https://en.wikipedia.org/wiki/ChaCha20-Poly1305
.. _ECDH: https://en.wikipedia.org/wiki/Elliptic-curve_Diffie%E2%80%93Hellman
.. _Curve25519: https://en.wikipedia.org/wiki/Curve25519
.. _MbedTLS: https://github.com/Mbed-TLS/mbedtls
.. _PSA: https://www.psacertified.org/
.. _ASCON: https://ascon.iaik.tugraz.at/
.. _NIST LWC: https://csrc.nist.gov/projects/lightweight-cryptography
.. _Nordic Identity Service: https://docs.nordicsemi.com/bundle/nrf-cloud/page/SecurityServices/IdentityService/IdentityOverview.html
