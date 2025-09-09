.. _board_tauro_2:

Embeint Tauro 2
###############

Overview
********

The Tauro 2 hardware is designed as a research data collection device,
saving a 1Hz GNSS data trace and 120Hz 9-axis IMU trace to an external
SD card for later retrieval.

Functionally the Tauro 2 is similar to the original Tauro, with improved
power efficiency from a 1.8V primary rail and the new nRF54L15 Bluetooth
controller.

Hardware
********

Expected Idle Power Consumption
===============================

Standard operation for the Tauro collar is the GNSS modem running full time.
As such, the hardware was not optimised for the lowest possible idle current
consumption.

+------------------+------------------------+------------------+
|           Device | Description            | Consumption (uA) |
+==================+========================+==================+
|   Nordic nRF9151 | LTE Modem &            |                5 |
|                  | Application processor  |                  |
+------------------+------------------------+------------------+
|  Nordic nRF54L15 | Bluetooth controller   |              ??? |
+------------------+------------------------+------------------+
|      TI TPS62843 | DC/DC buck converter   |              0.4 |
+------------------+------------------------+------------------+
|           INA236 | Power Monitor          |              2.2 |
+------------------+------------------------+------------------+
|          BQ25186 | Battery Charger        |                4 |
+------------------+------------------------+------------------+
| u-blox MAX-M10S  | GNSS modem             |               37 |
+------------------+------------------------+------------------+
|          LSM6DSV | 6-axis IMU             |              2.6 |
+------------------+------------------------+------------------+
|           IIS2MD | 3-axis Magnetometer    |              1.5 |
+------------------+------------------------+------------------+
|            SHT41 | Temp + Humidity Sensor |              0.1 |
+------------------+------------------------+------------------+
|          LPS22HH | Pressure Sensor        |              0.9 |
+------------------+------------------------+------------------+
|        W25Q128JW | External flash         |                1 |
+------------------+------------------------+------------------+
|                  | TOTAL                  |              ~80 |
+------------------+------------------------+------------------+
