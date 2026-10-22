.. _bluetooth-hci-spi-sample:

Bluetooth: HCI SPI
##################

Overview
********

Expose the Bluetooth controller over SPI to another device/CPU using the Zephyr SPI HCI transport
protocol (similar to BlueNRG).

This is the Zephyr ``samples/bluetooth/hci_spi`` application with Infuse-IoT additions:

* Infuse-IoT application ID (``0xB7000001``) and versioning.
* ePacket and RPC support (``APPLICATION_INFO`` and ``FILE_WRITE_BASIC``), allowing the host to
  query and update the controller firmware.
* Reduced connection and advertising set counts on memory constrained SoCs.

Requirements
************

A board with SPI peripheral, GPIO and Bluetooth Low Energy support. The devicetree must define a
node with compatible ``zephyr,bt-hci-spi-slave``, which selects the SPI bus and the interrupt line
to the host.

Overlays are provided in ``boards/`` for:

* ``auroch/nrf54l15/cpuapp``
* ``tauro/nrf52840``
* ``tauro2/nrf54l15/cpuapp``
* ``nrf9160dk/nrf52840``

The host requires a ``zephyr,bt-hci-spi`` node describing the connection to this device.

Building
********

Sysbuild is configured to apply the ``infuse-bt-ctlr`` snippet to MCUboot. Apply the same snippet
to the application for Infuse-IoT integration:

.. code-block:: bash

   west build -b tauro/nrf52840 --sysbuild infuse-sdk/apps/bluetooth/hci_spi -- \
      -DSNIPPET=infuse-bt-ctlr
