.. _embedded_sample_low_power:

Low Power
#########

Sample application that simply goes into the default low power state.
Useful for validating hardware and drivers are low power by default.

Pico idle-power validation
**************************

Build for Pico, Pico W, Pico 2 or Pico 2 W without the ``usb`` snippet::

   west build -b rpi_pico/rp2040/w -S infuse samples/low_power

The other targets are ``rpi_pico/rp2040``, ``rpi_pico2/rp2350a/m33`` and
``rpi_pico2/rp2350a/m33/w``. USB and wireless remain disabled so they do not
keep the board active during idle-power validation. W-model onboard LEDs
remain inactive, and no wireless firmware blobs are required.

Use ``samples/releases/serial`` with ``-S usb`` for USB logging, telemetry and
RPC bring-up. See :ref:`infuse-vendor-raspberrypi` for provisioning and the
RP2040 randomness limitation.
