.. _board_thingy91x:

Nordic Thingy:91 X
##################

Overview
********

The `Nordic Thingy:91 X`_ is a versatile prototyping platform designed for
the development of cellular IoT applications. It is built around the nRF9151
System-in-Package (SiP) which supports LTE-M, NB-IoT, GNSS, and NR+, making
it suitable for global use. This platform is particularly ideal for
asset-tracking applications due to its capability to utilize multiple location
tracking methods including cellular, Wi-Fi, and GNSS in conjunction with nRF
Cloud Location Services. The device is also equipped with a comprehensive array
of sensors that monitor environmental conditions and movement, enhancing its
utility in various IoT applications.

Configuration
*************

The Thingy:91 X board in Infuse-IoT is configured such that the nRF9151 SiP is
the primary application microcontroller, controlling all peripherals on the
board. The nRF5340 SoC runs solely as a Bluetooth controller, with the Bluetooth
host running on the nRF9151.

The only limitation of this configuration is that the nRF9151 lacks the RAM required
to run the nRF7002 Wi-Fi co-processor in any mode other that SSID scanning. As such,
the definitions pretend that a nRF7000 is loaded instead.

Peripheral Support
******************

The Infuse-IoT board definition currently supports:
 * nRF9151 LTE & GNSS
 * nRF5340 Bluetooth controller
 * BMI270 6-axis IMU
 * BME688 environmental sensor
 * nPM1300 battery and charge monitoring
 * 256 Mbit external flash
 * RGB LED

Features not yet supported:
 * Infuse-IoT Bluetooth controller snippet (DFU & Versioning)
 * USB serial passthrough
 * ADXL367 low power accelerometer
 * BMM350 magnetometer

.. _Nordic Thingy:91 X: https://www.nordicsemi.com/Products/Development-hardware/Nordic-Thingy-91-X
