.. _gateway_lte:

Gateway LTE
###########

Overview
********

A core application that bridges Bluetooth devices to the Infuse-IoT cloud over an LTE network.

* Bluetooth advertising packets are received continuously and a subset are forwarded to the cloud
  (see `Bluetooth Forwarding`_).
* Packets from Bluetooth connections (up to 4) are forwarded to the cloud.
* Packets from the cloud are routed to the destination Bluetooth device.

Requirements
************

The application supports two modem architectures:

* **Native nRF91**: The application runs on the nRF91 itself (e.g. ``auroch/nrf9151/ns``).
* **External modem**: The application runs on a Bluetooth SoC that controls an LTE modem over UART
  through the Zephyr ``modem_cellular`` driver (e.g. ``bonasus@0/nrf54l15/cpuapp``,
  ``kudu/nrf54l15/cpuapp``, ``nrf93m1dk/nrf54l15/cpuapp``).

The ``fuel_gauge0`` devicetree alias is required. ``environmental0`` and ``gnss`` are optional and
enable the corresponding tasks when present.

Building
********

The ``infuse`` snippet is applied automatically by the application ``CMakeLists.txt``.

.. code-block:: bash

   west build -b auroch/nrf9151/ns --sysbuild infuse-sdk/apps/gateway_lte

LTE Configuration
*****************

Defaults are tuned for an always-on, mains-powered gateway:

* LTE-M network mode.
* PSM disabled.
* eDRX enabled with a 20.48 second cycle.
* The UDP interface is always listening for downlink packets. If no valid downlink is received for
  4 hours, the UDP downlink watchdog reboots the device to recover from a bad networking state.

On nRF91 devices the application requests a default Bluetooth throughput limit of 175 kbps
(``KV_KEY_BLUETOOTH_THROUGHPUT_LIMIT``) from connected devices, to avoid overrunning the LTE uplink.
The value is only written if the key does not already exist. The default is controlled by
``CONFIG_LTE_GATEWAY_DEFAULT_MAXIMUM_UPLINK_THROUGHPUT_KBPS``.

Bluetooth Forwarding
********************

By default, 25% of received Bluetooth advertising packets are forwarded to the cloud. For packets
that are dropped, the RSSI of the source device is still reported to the cloud.

This behaviour is configured at runtime through the ``KV_KEY_GATEWAY_BLUETOOTH_FORWARD_OPTIONS``
KV store key. Changes apply immediately, and deleting the key reverts to the defaults.

While the ``INFUSE_STATE_HIGH_PRIORITY_UPLINK`` state is set, Bluetooth scanning is paused to
prioritise the uplink.

Periodic Behaviour
******************

.. list-table::
   :header-rows: 1

   * - Task
     - Period
     - Notes
   * - TDF logger (UDP)
     - 5 minutes
     - Announce, battery, environmental, location and network state, followed by any pending
       Memfault chunks. Also sent on the first network connection after boot.
   * - TDF logger (Bluetooth)
     - 1 second
     - Announce, battery, environmental, location and network state
   * - Battery
     - 30 seconds
     -
   * - Environmental
     - 30 seconds
     - If ``environmental0`` exists
   * - GNSS
     - 30 minutes
     - If ``gnss`` exists. First run 5 minutes after boot, runs until a location fix is obtained
       (2 minute timeout). The resulting location is logged to flash.

Reboot information, LTE connection state, time synchronisation and Bluetooth connection events are
logged to flash.

LEDs
****

On boot all LEDs flash 5 times. Afterwards one LED pulses every 5 seconds to indicate the
connection state:

.. list-table::
   :header-rows: 1

   * - LED
     - Pulse
     - Meaning
   * - ``led0``
     - 1 second
     - LTE modem is not searching or registered
   * - ``led1``
     - 200 ms
     - Connected to the Infuse-IoT cloud
   * - ``led2``
     - 200 ms
     - LTE modem active, but not yet connected to the cloud
