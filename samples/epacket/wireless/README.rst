.. _sample-epacket-wireless:

Wireless ePacket
################

This sample sends announce telemetry every five seconds over USB, BLE GATT
and Wi-Fi UDP. It supports Pico 2 W and nRF7002 DK application-core builds.
Pico W requires an additional secure entropy source; its ROSC driver leaves
cryptographic randomness disabled. Network credentials are stored in the
usual Infuse KV keys.

The optional ``status-led`` alias identifies a Zephyr LED API device; LED
index 0 blinks when that alias is present. The Pico wireless overlay provides
its CYW43439 LED through this alias. No board-specific GPIO code is needed.

When the ePacket advertising interface is enabled, it owns connectable
advertising and also broadcasts announce TDFs. Otherwise the sample starts
legacy connectable advertising with the Infuse service UUID and restarts it
after disconnect. The Pico controller supports the legacy/GATT path only;
the Nordic build enables the normal extended-advertising interface.

Build and flash
***************

Fetch the Wi-Fi and Bluetooth firmware::

   west blobs fetch hal_infineon --allow-regex '.*43439A0\.(bin|clm_blob)$'
   west blobs fetch infuse-sdk

Build any supported board::

   west build -b rpi_pico2/rp2350a/m33/w samples/epacket/wireless -d build/pico2-w-wireless
   west blobs fetch nrf_wifi
   west build -b nrf7002dk/nrf5340/cpuapp samples/epacket/wireless -d build/nrf7002-wireless

The application includes ``infuse`` and ``usb`` on all boards. Only the two
Pico W targets add ``infuse-pico-wireless``. The Nordic target uses its nRF70
Wi-Fi driver and Bluetooth IPC HCI; an appropriate network-core controller
image is required for hardware use. Its coverage here is build-only.

For Pico boards, hold BOOTSEL while connecting, then flash::

   picotool load -v build/pico2-w-wireless/zephyr/zephyr.uf2
   picotool reboot

Provisioning and settings retain the flash layout from :ref:`infuse-vendor-raspberrypi`
and are excluded from UF2 images. Re-enter BOOTSEL manually for subsequent
flashes. See :ref:`infuse-vendor-raspberrypi` for provisioning and identity.

Network credentials
*******************

Store the usual Infuse ``WIFI_SSID`` and ``WIFI_PSK`` KV values using the
``kv_write`` RPC over USB or BLE. Use the normal KV string encoding (a length byte followed by the UTF-8
string and its terminating NUL); the Python ``wifi_configure`` RPC command
encodes these values automatically. The sample
enables ``kv_read`` and ``kv_write``; authenticated commands require the normal
Infuse identity/key setup. Do not put passwords into source control.

With no SSID stored, the sample performs a Wi-Fi scan, reports the number of
access points found, and continues USB/BLE operation without joining a network.
With credentials stored, the existing connection manager joins the network,
obtains IPv4 by DHCP and reconnects after disconnection. The default UDP endpoint
is ``udp.dev.infuse-iot.com:3001``; use the normal UDP KV configuration to change
it. Network/cloud delivery also requires the appropriate provisioning and keys.

Validation
**********

Build coverage includes both Pico W boards and nRF7002 DK. The QEMU transport tests exercise ring
wraparound, partial frames, malformed/oversized packets, transient and persistent
invalid indices, invalid shared-memory bases, firmware bounds and bus failure
propagation::

   west build -b mps2/an385 tests/drivers/cyw43_transport -d build/test-cyw43
   west build -d build/test-cyw43 -t run

Hardware coverage is recorded in :ref:`infuse-vendor-raspberrypi`. A successful
build alone does not validate radio range, simultaneous network throughput,
access-point compatibility or power consumption.
