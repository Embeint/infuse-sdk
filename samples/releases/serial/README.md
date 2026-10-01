# Serial Device Demo

A demonstration application that is permanently available for serial
communications, enabling testing of release processes.

## Pico 2 USB bring-up

Build for Pico 2 or Pico 2 W using the existing USB snippet:

```sh
west build -b rpi_pico2/rp2350a/m33/w -S infuse -S usb samples/releases/serial
```

Use `rpi_pico2/rp2350a/m33` for Pico 2. Hold BOOTSEL while connecting USB,
then copy `build/zephyr/zephyr.uf2` to the mounted drive. The application
provides announce telemetry and echo RPCs through the serial ePacket interface.
Start `infuse gateway --serial <port> --display-only` to inspect telemetry.
The optional onboard LED is skipped if its controller is unavailable or fails.
On W models the LED requires an initialized wireless chip; USB-only builds
leave wireless disabled and need no wireless firmware blobs.

Pico/Pico W RP2040 targets need an additional secure entropy source for this
sample. Their ROSC driver deliberately does not enable cryptographic random
generation. Use `samples/low_power` with `-S usb` for logging on those boards.
