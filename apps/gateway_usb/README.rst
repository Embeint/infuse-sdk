.. _gateway_usb:

Gateway USB
###########

Overview
********

A core application that bridges Bluetooth devices to a local host PC over a USB CDC-ACM
connection.

* All received Bluetooth advertising packets are forwarded to the host.
* Packets from Bluetooth connections are forwarded to the host.
* Packets from the host are routed to the destination Bluetooth device.

On the host, the gateway is used through ``infuse gateway`` (see :ref:`python_gateways`), which
then allows other ``infuse`` commands to interact with nearby Bluetooth devices.

Requirements
************

A board with Bluetooth and USB device support, for example:

* ``nrf52840dk/nrf52840``
* ``nrf5340dk/nrf5340/cpuapp``
* ``nrf54lm20dk/nrf54lm20b/cpuapp``

Building
********

The ``infuse`` and ``usb`` snippets are applied automatically by the application
``CMakeLists.txt``.

.. code-block:: bash

   west build -b nrf52840dk/nrf52840 infuse-sdk/apps/gateway_usb
   west flash

After flashing, connect to the gateway over the new USB serial port:

.. code-block:: bash

   infuse gateway --serial /dev/ttyACM0

Behaviour
*********

* On boot, the device key identifiers are sent to the host.
* An announce and battery packet is sent to the host every minute.
* Bluetooth connection events are sent to the host as they occur.
* If the board has a ``led0`` alias, the LED blinks once a second as proof of life.
