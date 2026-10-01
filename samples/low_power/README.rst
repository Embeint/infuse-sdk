.. _embedded_sample_low_power:

Low Power
#########

Sample application that simply goes into the default low power state.
Useful for validating hardware and drivers are low power by default.

Pico USB logging
****************

Build this sample with the ``usb`` snippet on Pico, Pico W, Pico 2 or Pico 2 W::

   west build -b rpi_pico/rp2040/w -S infuse -S usb samples/low_power

The other targets are ``rpi_pico/rp2040``, ``rpi_pico2/rp2350a/m33`` and
``rpi_pico2/rp2350a/m33/w``. Hold BOOTSEL while connecting USB and copy
``build/zephyr/zephyr.uf2`` to the mounted Raspberry Pi drive. Open the new
serial port at 115200 baud for uptime logs. The application starts without
waiting for a terminal. USB remains enabled, so this profile demonstrates
bring-up and does not measure minimum idle current.

W-model wireless chips and their onboard LEDs remain inactive. No firmware
blobs or network credentials are required. RP2040 ROSC is not suitable for
cryptographic randomness; this profile leaves Infuse security and ePacket off.
See :ref:`infuse-vendor-raspberrypi` for the flash layout and provisioning.
