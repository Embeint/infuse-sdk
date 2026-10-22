.. _bluetooth-hci-uart-sample:

Bluetooth: HCI UART
###################

Overview
********

Expose the Bluetooth controller over UART to another device/CPU using the H:4 HCI transport
protocol (requires hardware flow control from the UART).

This is the Zephyr ``samples/bluetooth/hci_uart`` application with Infuse-IoT additions:

* Infuse-IoT application ID (``0xB7000002``) and versioning.
* ePacket and RPC support (``APPLICATION_INFO`` and ``FILE_WRITE_BASIC``), allowing the host to
  query and update the controller firmware.
* Reduced connection and advertising set counts on memory constrained SoCs.

Requirements
************

A board with Bluetooth Low Energy support. The UART connected to the host is selected by the
``zephyr,bt-c2h-uart`` chosen node.

Default UART settings
*********************

By default the controller builds use the following settings:

* Baudrate: 1Mbit/s
* 8 bits, no parity, 1 stop bit
* Hardware Flow Control (RTS/CTS) enabled

Building
********

Sysbuild is configured to apply the ``infuse-bt-ctlr`` snippet to MCUboot. Apply the same snippet
to the application for Infuse-IoT integration:

.. code-block:: bash

   west build -b auroch/nrf54l15/cpuapp --sysbuild infuse-sdk/apps/bluetooth/hci_uart -- \
      -DSNIPPET=infuse-bt-ctlr
