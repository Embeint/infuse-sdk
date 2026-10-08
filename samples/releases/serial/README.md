# Serial Device Demo

A demonstration application that is permanently available for serial
communications, enabling testing of release processes.

## Pico USB bring-up

Build for Pico, Pico W, Pico 2 or Pico 2 W using the existing USB snippet:

```sh
west build -b rpi_pico2/rp2350a/m33/w -S infuse -S usb samples/releases/serial
```

The other targets are `rpi_pico/rp2040`, `rpi_pico/rp2040/w` and
`rpi_pico2/rp2350a/m33`. Hold BOOTSEL while connecting USB,
then copy `build/zephyr/zephyr.uf2` to the mounted drive. The application
provides announce telemetry and RPCs through the serial ePacket interface.
Start `infuse gateway --serial <port> --display-only` to inspect telemetry.
The existing LED loop runs on boards that define a `led0` alias. Pico USB-only
builds have no such alias. On W models the LED requires an initialized wireless
chip; USB-only builds leave wireless disabled and need no wireless firmware blobs.

**Warning:** Pico/Pico W RP2040 targets keep all Infuse features enabled, but
use Mbed TLS's legacy DRBG seeded by Zephyr's insecure timer-based test RNG.
The upstream ROSC driver does not enable cryptographically secure randomness
or seed this fallback. Key generation and protocol security require an
additional secure entropy source; builds warn about this limitation.
