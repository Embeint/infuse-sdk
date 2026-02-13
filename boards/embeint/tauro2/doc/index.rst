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

Standard operation for the Tauro 2 collar is the GNSS modem running full time.
As such, the hardware was not optimised for the lowest possible idle current
consumption.

Expected Idle Power Consumption (1.8V Rail)
===========================================

+------------------+------------------------+------------------+
|           Device | Description            | Consumption (uA) |
+==================+========================+==================+
|  Nordic nRF54L15 | Bluetooth controller   |              3.0 |
+------------------+------------------------+------------------+
| u-blox MAX-M10S  | GNSS modem             |             37.0 |
+------------------+------------------------+------------------+
|          LSM6DSV | 6-axis IMU             |              2.6 |
+------------------+------------------------+------------------+
|           IIS2MD | 3-axis Magnetometer    |              1.5 |
+------------------+------------------------+------------------+
|            SHT41 | Temp + Humidity Sensor |              0.1 |
+------------------+------------------------+------------------+
|          LPS22HH | Pressure Sensor        |              0.9 |
+------------------+------------------------+------------------+
|        W25Q128JW | External flash         |              1.0 |
+------------------+------------------------+------------------+
|                  | TOTAL                  |             46.1 |
+------------------+------------------------+------------------+


Expected Idle Power Consumption (SYS Rail)
===========================================

+------------------+------------------------+------------------+
|           Device | Description            | Consumption (uA) |
+==================+========================+==================+
|   Nordic nRF9151 | LTE Modem &            |              2.2 |
|                  | Application processor  |                  |
+------------------+------------------------+------------------+
|          BQ25186 | Battery Charger        |              3.0 |
+------------------+------------------------+------------------+
|           INA236 | Power Monitor          |              2.2 |
+------------------+------------------------+------------------+
|      TI TPS62843 | DC/DC buck converter   |              0.3 |
+------------------+------------------------+------------------+
|          LPS5817 | LED Driver             |              0.1 |
+------------------+------------------------+------------------+
|                  | TOTAL                  |              7.8 |
+------------------+------------------------+------------------+

Expected Idle Power Consumption (TOTAL)
=======================================

For Vin = 3.6V, efficiency of the 1.8V buck regulator:
55% at Iout = 1uA
80% at Iout = 5uA
85% at Iout = 10uA
90% at Iout = 100uA

46uA @ 1.8V is equivalent to 23uA @ 3.6V, 27uA including the efficiency loss.
Combined with the higher voltage rails, the total power pulled from VBAT is expected to be approximately 35uA.

Measured Idle Power Consumption
===============================

Running ``infuse-sdk/samples/validation``, the idle power consumption after testing finishes has been measured
as 45 uA, roughly matching the expected value.
