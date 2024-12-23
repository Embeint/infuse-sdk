.. _board_tauro:

Embeint Tauro
#############

Overview
********

The Tauro hardware is designed as a research data collection device,
saving a 1Hz GNSS data trace and 120Hz 9-axis IMU trace to an external
SD card for later retrieval.

Hardware
********

Expected Idle Power Consumption
===============================

Standard operation for the Tauro collar is the GNSS modem running full time.
As such, the hardware was not optimised for the lowest possible idle current
consumption.

+------------------+-----------------------+------------------+
|           Device | Description           | Consumption (uA) |
+==================+=======================+==================+
|   Nordic nRF9151 | LTE Modem &           |                5 |
|                  | Application processor |                  |
+------------------+-----------------------+------------------+
|  Nordic nRF52840 | Bluetooth controller  |               18 |
+------------------+-----------------------+------------------+
|     Torex XC6201 | Linear Regulator      |                2 |
+------------------+-----------------------+------------------+
|          BQ25185 | Battery Charger       |                4 |
+------------------+-----------------------+------------------+
| u-blox SAM-M10Q  | GNSS modem            |               46 |
+------------------+-----------------------+------------------+
|          LSM6DSV | 6-axis IMU            |              2.6 |
+------------------+-----------------------+------------------+
|           IIS2MD | 3-axis Magnetometer   |              1.5 |
+------------------+-----------------------+------------------+
|           BME280 | Environmental Sensor  |              0.1 |
+------------------+-----------------------+------------------+
|        W25Q128JV | External flash        |                1 |
+------------------+-----------------------+------------------+
|                  | TOTAL                 |              ~80 |
+------------------+-----------------------+------------------+
