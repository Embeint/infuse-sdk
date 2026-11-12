.. _gateway_bt:

Gateway Bluetooth
#################

Overview
********

A core application that bridges Bluetooth devices to a Bluetooth central (for example a phone)
that is connected to the gateway. The Bluetooth peripheral connection is used as the ePacket
backhaul, which allows the gateway to act as a relay to extend the range of the central.

* While a backhaul connection exists, received Bluetooth advertising packets are forwarded over it.
* Packets from Bluetooth connections are forwarded over the backhaul.
* Packets from the backhaul are routed to the destination Bluetooth device, including
  ``INFUSE_EPACKET_FORWARD_AUTO_CONN`` packets that automatically create the connection.

Requirements
************

A board with Bluetooth support, for example:

* ``nrf52840dk/nrf52840``
* ``nrf5340dk/nrf5340/cpuapp``
* ``nrf54l15dk/nrf54l15/cpuapp``
* ``bonasus/nrf54l15/cpuapp`` (with battery reporting)

Building
********

The ``infuse`` snippet is applied automatically by the application ``CMakeLists.txt``.

.. code-block:: bash

   west build -b nrf52840dk/nrf52840 --sysbuild infuse-sdk/apps/gateway_bt
   west flash

Behaviour
*********

* An announce packet is broadcast over connectable Bluetooth advertising every 2 seconds, and the
  device name is broadcast over legacy advertising.
* If the board has a ``fuel-gauge0`` alias, the battery is measured every 30 seconds and the battery
  state is included with the announce packets on both advertising and the backhaul.
* Bluetooth advertising is only scanned while a backhaul connection exists.
* An announce packet is sent over the backhaul every minute.
* Bluetooth connection events are sent over the backhaul as they occur.
* If the board has a ``led0`` alias, the LED is on while a backhaul connection exists.
* When the last backhaul connection is lost, all connections to Bluetooth devices are terminated.

Backhaul Clients
****************

Up to two centrals can be connected to the gateway at once. Packets sent over the backhaul are sent
to all connected clients. Additional clients are disconnected immediately, so that connection
resources remain available for forwarding (up to two simultaneous forwarding connections).

The SoftDevice Controller counts active connectable advertising sets against the peripheral role
limit, so the application reserves four peripheral roles (two clients, the legacy and ePacket
advertising sets). On the nRF5340, the network core controller configuration is extended through
``sysbuild/hci_ipc.conf``.
