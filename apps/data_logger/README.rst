.. _data_logger:

Data Logger
###########

Overview
********

A data gathering application that continuously samples the IMU, battery, environmental and
(if present) GNSS sensors, logging the raw data to the best available storage backend.

Requirements
************

The board devicetree must define the following aliases:

* ``imu0``: Accelerometer + gyroscope
* ``fuel_gauge0``: Battery monitoring
* ``environmental0``: Temperature/pressure/humidity sensor (``environmental1`` is optional)
* ``gnss`` (optional): u-blox GNSS modem

Tested platforms: ``thingy53/nrf5340/cpuapp[/ns]`` and ``tauro/nrf9151/ns``.

Building
********

The ``infuse`` snippet is applied automatically by the application ``CMakeLists.txt``.

.. code-block:: bash

   west build -b tauro/nrf9151/ns --sysbuild infuse-sdk/apps/data_logger

To enable logging over WiFi on the Thingy:53, add the nRF7002 EB shield and the UDP ePacket
overlay:

.. code-block:: bash

   west build -b thingy53/nrf5340/cpuapp --sysbuild infuse-sdk/apps/data_logger -- \
      -Ddata_logger_SHIELD=nrf7002eb \
      -Ddata_logger_EXTRA_DTC_OVERLAY_FILE=boards/shields/epacket_udp.overlay

Sampling
********

.. list-table::
   :header-rows: 1

   * - Sensor
     - Rate
     - Notes
   * - IMU
     - 50 Hz
     - Accelerometer (±4 G) and gyroscope (±500 dps), runs continuously
   * - Battery
     - Every 5 seconds
     -
   * - Environmental
     - Every 5 seconds
     -
   * - GNSS
     - Continuous
     - Starts above 30% battery, terminates below 20%

Logging Backends
****************

Sensor data, time synchronisation events and KV store changes are logged to a single storage
backend, selected at compile time:

1. **SD card** (``CONFIG_DATA_LOGGER_EXFAT``): Data is logged solely to the external SD card.
2. **Serial only**: If UDP is provided by the nRF91 LTE modem, it is not used for sensor data due
   to the excessive data volumes.
3. **Serial and UDP**: Otherwise (e.g. WiFi), data is streamed over both interfaces.

Independent of the storage backend, a status summary (announce, battery, environmental, location,
accelerometer and network state) is broadcast over Bluetooth advertising and serial every 2
seconds. If a UDP interface exists, the same summary is sent over UDP every 5 minutes, followed by
any pending Memfault chunks.

.. warning::
    Modifying this application to log sensor data to UDP over LTE WILL cost significant amounts
    of money!!!

Firmware Updates
****************

When ``CONFIG_INFUSE_DFU_EXFAT`` is enabled, the application checks the SD card for a newer
application image on boot. If one is found, it is copied into the secondary image slot and the
device reboots into the new image.
