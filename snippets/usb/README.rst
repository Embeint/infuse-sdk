.. _snippet-usb:

USB snippet (usb)
#################

Overview
********

Use the builtin USB hardware for serial communications and ePacket
instead of the default UART port.

The Pico family is supported through ``zephyr_udc0``. For Pico targets, this
snippet enables the new USB device stack and Infuse CDC ACM startup. Combine it
with ``infuse`` to use the existing serial telemetry sample::

   west build -b rpi_pico/rp2040 -S infuse -S usb samples/releases/serial
   west build -b rpi_pico2/rp2350a/m33/w -S infuse -S usb samples/releases/serial

All four boards support logging, telemetry and RPC. RP2040 keeps these features
enabled using an insecure timer-based RNG fallback; builds warn about this
limitation. Security guarantees require an additional secure entropy source.
See :ref:`infuse-vendor-raspberrypi` for provisioning and randomness limits.
USB startup does not wait for a host terminal or initialize the wireless chip.

Use ``samples/low_power`` without this snippet for idle-power validation.
