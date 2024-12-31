.. _epacket_api:

ePacket
#######

The Infuse-IoT ePacket interface is the core communication abstraction used by the platform.
By providing a secure and efficient interface, ePacket allows application logic to be written
independently of particular board hardware, enabling greater code reuse and stability.

Interfaces
**********

ePacket interfaces are the common interface through which individual packets are
sent and received. Each interface typically corresponds to a single mode of a communications
technology, for example UDP packets on an IP network. As a result, some technologies such
can have multiple interfaces co-existing simultaneously, for example Bluetooth Advertising,
GATT peripheral and GATT central.

All ePacket interfaces share a single dedicated thread for processing packet transmission
and reception.

Transmission
============

To transmit a packet over an interface, a packet buffer must first be claimed with
:c:func:`epacket_alloc_tx_for_interface`. This function automatically configures the
claimed buffer with the maximum payload size of the interface. Once the packet payload
is populated, the packet transmission metadata (:c:struct:`epacket_tx_metadata`) must
be populated through :c:func:`epacket_set_tx_metadata`.

Once constructed, packet buffers are queued for transmission through the :c:func:`epacket_queue`
function, which forwards the packet to the processing context. If the queueing context must be
notified of the result of transmission, a TX complete callback can be attached to individual packets
with the :c:func:`epacket_set_tx_callback` function.

Individual packets are treated as independent entities, with no guarantees about in-order
delivery or packet loss. Acknowledgements on individual packets can be requested with
the :c:enumerator:`EPACKET_FLAGS_ACK_REQUEST` flag.

Reception
=========

Depending on the technology backend, there are several options for when packets can be received
on an interface:

  1. Reception enabled by the application through :c:func:`epacket_receive`:

    * Bluetooth Advertising
    * UART Serial

  2. Reception automatically enabled due to application state:

    * Bluetooth GATT peripheral
    * Bluetooth GATT central
    * USB Serial
    * UDP

When a packet is received, the default packet handler is :c:func:`epacket_default_receive_handler`.
This can be updated on a per interface basis with :c:func:`epacket_set_receive_handler`.

Additional Callbacks
====================

Applications can register for additional callbacks on an interface with :c:func:`epacket_register_callback`.

  1. Interface state has changed (connected/disconnected/MTU update)
  2. Transmission on the interface has failed
  3. Packet has been received on the interface

Security
========

Communications over each interface are secured through one of two mechanisms.

Infuse-IoT Security Model
-------------------------

Most communications interfaces are implemented using the Infuse-IoT :ref:`platform-security-model`,
which uses `HKDF`_ together with the Infuse-IoT cloud public key to generate a secure key-chain
per device. `ChaCha20-Poly1305`_ is then used as an application level encryption scheme to protect
individual packets.

Specification Mandated Security
-------------------------------

For communications interfaces which have their own authentication and encryption mechanism
defined as part of the specificiation (e.g. `LoRaWAN`_), the ePacket interface can re-use
those mechanisms to avoid inefficiencies (e.g. double encryption of payloads).

API Reference
*************

.. doxygengroup:: epacket_interface_apis
.. doxygengroup:: epacket_packet_apis

.. _HKDF: https://en.wikipedia.org/wiki/HKDF
.. _ChaCha20-Poly1305: https://en.wikipedia.org/wiki/ChaCha20-Poly1305
.. _LoRaWAN: https://lora-alliance.org/
