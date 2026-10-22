.. _lte_tracker:

LTE Tracker
###########

Overview
********

A basic tracking application for nRF91 devices. Instead of GNSS, the device location is estimated
in the cloud from periodic scans of nearby LTE cells and, optionally, WiFi access points.

Requirements
************

An nRF91 series board, for example:

* ``nrf9151dk/nrf9151/ns``, ``nrf9160dk/nrf9160/ns``, ``nrf9161dk/nrf9161/ns``
* ``tauro/nrf9151/ns``, ``tauro2/nrf9151/ns``
* ``thingy91x/nrf9151/ns``

The ``fuel_gauge0`` devicetree alias is required. ``environmental0`` is optional.

Building
********

The ``infuse`` snippet is applied automatically by the application ``CMakeLists.txt``.

.. code-block:: bash

   west build -b nrf9151dk/nrf9151/ns --sysbuild infuse-sdk/apps/lte_tracker

To also scan for WiFi access points with an nRF7002 EK shield:

.. code-block:: bash

   west build -b nrf9151dk/nrf9151/ns --sysbuild infuse-sdk/apps/lte_tracker -- \
      -Dlte_tracker_SHIELD=nrf7002ek \
      -Dlte_tracker_EXTRA_CONF_FILE=wifi_scanning.conf

Behaviour
*********

.. list-table::
   :header-rows: 1

   * - Task
     - Period
     - Notes
   * - Network scan
     - 5 minutes
     - Scans for up to 4 LTE cells. With WiFi scanning enabled, up to 8 access points are also
       reported, and the LTE scan is skipped if enough access points are found. Results are sent
       over serial and UDP.
   * - TDF logger (UDP)
     - After each network scan
     - Announce, battery, environmental and network state over serial and UDP
   * - TDF logger (serial)
     - 2 seconds
     - Announce, battery and network state
   * - TDF logger (Bluetooth)
     - 900 ms
     - If Bluetooth is enabled. Announce, battery, environmental, location and network state
   * - Battery
     - 10 seconds
     -
   * - Environmental
     - 5 seconds
     - If ``environmental0`` exists

KV store changes are logged over UDP, as the device may not have flash storage for logging.
