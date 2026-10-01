.. _snippet-usb:

USB snippet (usb)
#################

Overview
********

Use the builtin USB hardware for serial communications and ePacket
instead of the default UART port.

The Pico family is supported through ``zephyr_udc0``. For Pico targets, this
snippet enables the new USB device stack and Infuse CDC ACM startup. Combine it
with ``infuse`` to use the existing logging or serial telemetry samples::

   west build -b rpi_pico/rp2040 -S infuse -S usb samples/low_power
   west build -b rpi_pico2/rp2350a/m33/w -S infuse -S usb samples/releases/serial

RP2040 supports logging; secure ePacket/RPC needs another secure entropy source.
See :ref:`infuse-vendor-raspberrypi` for provisioning and randomness limits.
USB startup does not wait for a host terminal or initialize the wireless chip.
