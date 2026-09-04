.. _board_bonasus:

Embeint Bonasus
############

Overview
********

The Bonasus hardware is a CAT1-bis LTE Bluetooth gateway for high throughput applications.

Hardware
********

Expected Idle Power Consumption (1.8V Rail)
===========================================

+------------------+------------------------+------------------+
|           Device | Description            | Consumption (uA) |
+==================+========================+==================+
|  Nordic nRF54L15 | Application MCU        |              3.0 |
+------------------+------------------------+------------------+
| u-blox MAX-M10S  | GNSS modem             |             37.0 |
+------------------+------------------------+------------------+
|          LSM6DSV | 6-axis IMU             |              2.6 |
+------------------+------------------------+------------------+
|            SHT41 | Temp + Humidity Sensor |              0.1 |
+------------------+------------------------+------------------+
|          LPS22HH | Pressure Sensor        |              0.9 |
+------------------+------------------------+------------------+
|        W25Q128JW | External flash         |              1.0 |
+------------------+------------------------+------------------+
|                  | TOTAL                  |             44.6 |
+------------------+------------------------+------------------+


Expected Idle Power Consumption (SYS Rail)
===========================================

+------------------+------------------------+------------------+
|           Device | Description            | Consumption (uA) |
+==================+========================+==================+
|          BQ25798 | Battery Charger        |             17.0 |
+------------------+------------------------+------------------+
|      TI TPS62830 | DC/DC buck converter   |              7.0 |
+------------------+------------------------+------------------+
|      TI TPS62830 | DC/DC buck converter   |              0.1 |
+------------------+------------------------+------------------+
|          LPS5815 | LED Driver             |              0.1 |
+------------------+------------------------+------------------+
|                  | TOTAL                  |             24.2 |
+------------------+------------------------+------------------+

Expected Idle Power Consumption (TOTAL)
=======================================

For Vin = 4.2V, efficiency of the 1.8V buck regulator:
40% at Iout = 1uA
80% at Iout = 100uA

46uA @ 1.8V is equivalent to 19uA @ 4.2V, 47uA including the efficiency loss.
Combined with the higher voltage rails, the total power pulled from VBAT is expected to be approximately 70uA.
