.. _bluetooth-hci-uart-sample:

Bluetooth: HCI UART
###################

Overview
*********

Expose Bluetooth controller support over UART to another device/CPU
using the H:4 HCI transport protocol (requires HW flow control from the UART).

Requirements
************

* A board with Bluetooth LE support

Default UART settings
*********************

By default the controller builds use the following settings:

* Baudrate: 1Mbit/s
* 8 bits, no parity, 1 stop bit
* Hardware Flow Control (RTS/CTS) enabled
