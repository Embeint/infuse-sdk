.. _gateway_uart:

Gateway UART
############

Overview
********

A core application that bridges Bluetooth devices to a local host PC over a UART connection,
for boards without a native USB device interface. The UART is typically exposed to the host
through a USB-UART bridge or debugger virtual COM port.

* All received Bluetooth advertising packets are forwarded to the host.
* Packets from Bluetooth connections are forwarded to the host.
* Packets from the host are routed to the destination Bluetooth device.

On the host, the gateway is used through ``infuse gateway`` (see :ref:`python_gateways`), which
then allows other ``infuse`` commands to interact with nearby Bluetooth devices.

The application logic is shared with :ref:`gateway_usb` (see ``apps/common/gateway_serial``).

Requirements
************

A board with Bluetooth support and ``epacket_serial`` provided by the ``infuse`` snippet, for
example:

* ``nrf52840dk/nrf52840``

Boards with a native USB device interface should use :ref:`gateway_usb` instead.

Building
********

The ``infuse`` snippet is applied automatically by the application ``CMakeLists.txt``.

.. code-block:: bash

   west build -b nrf52840dk/nrf52840 infuse-sdk/apps/gateway_uart
   west flash

After flashing, connect to the gateway over the board's serial port:

.. code-block:: bash

   infuse gateway --serial /dev/ttyACM0

Behaviour
*********

* On boot, the device key identifiers are sent to the host.
* An announce and battery packet is sent to the host every minute.
* Bluetooth connection events are sent to the host as they occur.
* If the board has a ``led0`` alias, the LED blinks once a second as proof of life.
