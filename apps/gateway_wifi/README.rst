.. _gateway_wifi:

Gateway WiFi
############

Overview
********

A core application that bridges Bluetooth devices to the Infuse-IoT cloud over a WiFi network.

* All received Bluetooth advertising packets are forwarded to the cloud.
* Packets from Bluetooth connections and the serial interface are forwarded to the cloud.
* Packets from the cloud are routed to the destination Bluetooth device.

The application is tuned for throughput rather than power consumption, making it suitable for
bulk data transfers from Bluetooth devices.

Requirements
************

A board with Bluetooth and WiFi support, for example ``nrf7002dk/nrf5340/cpuapp``. TF-M builds
(``/ns`` board variants) are not supported.

Building
********

The ``infuse`` snippet is applied automatically by the application ``CMakeLists.txt``.

.. code-block:: bash

   west build -b nrf7002dk/nrf5340/cpuapp --sysbuild infuse-sdk/apps/gateway_wifi

WiFi Configuration
******************

WiFi network credentials are read from the KV store. The application starts the network connection
on boot, and the connection is re-established if it drops.

Behaviour
*********

* An announce and network state packet is sent to the cloud every 5 seconds, and broadcast over
  Bluetooth advertising every 4.5 seconds.
* Device key identifiers are sent over serial on boot.
* Bluetooth and WiFi connection events are logged over serial.
* If the board has a ``led0`` alias, the LED is on while connected to the Infuse-IoT cloud.

Performance
***********

Throughput tends to be CPU limited, so the application:

* Runs the nRF5340 application core at 128 MHz.
* Builds with speed optimisations.
* Disables the packet discarding behaviour of the nRF70 driver.
* Requests a UDP acknowledgment after 5 minutes without hearing from the cloud (default 1 hour),
  so a lost server is detected sooner.

The ``zperf`` upload RPC (``CONFIG_INFUSE_RPC_COMMAND_ZPERF_UPLOAD``) is enabled to measure the
achievable WiFi throughput.
