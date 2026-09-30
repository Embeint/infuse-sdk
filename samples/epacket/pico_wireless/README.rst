.. _sample-epacket-pico-wireless:

Pico W wireless ePacket
######################

This bare-board sample supports Pico W and Pico 2 W. Every five seconds it sends
announce telemetry over USB, BLE GATT and Wi-Fi UDP, and blinks
the onboard LED. GATT telemetry is available to a connected Infuse client. Legacy connectable
advertising includes the Infuse service UUID and device name. The controller
does not support Infuse extended-advertising telemetry.
The Infuse common boot sequence enables Bluetooth; the Wi-Fi connection manager
uses credentials stored in the KV store. USB remains available for logs and RPCs.

Build and flash
***************

Fetch the Wi-Fi and Bluetooth firmware::

   west blobs fetch hal_infineon --allow-regex '.*43439A0\.(bin|clm_blob)$'
   west blobs fetch infuse-sdk

Build either board::

   west build -b rpi_pico/rp2040/w samples/epacket/pico_wireless -d build/pico-w-wireless
   west build -b rpi_pico2/rp2350a/m33/w samples/epacket/pico_wireless -d build/pico2-w-wireless

The application automatically includes ``infuse``, ``usb`` and
``infuse-pico-wireless``. Hold BOOTSEL while connecting the board, then flash::

   picotool load -v build/pico-w-wireless/zephyr/zephyr.uf2
   picotool reboot

Provisioning and settings retain the flash layout from :ref:`sample-epacket-usb`
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

Build coverage includes both W boards. The QEMU transport tests exercise ring
wraparound, partial frames, malformed/oversized packets, transient and persistent
invalid indices, invalid shared-memory bases, firmware bounds and bus failure
propagation::

   west build -b mps2/an385 tests/drivers/cyw43_transport -d build/test-cyw43
   west build -d build/test-cyw43 -t run

Hardware coverage is recorded in :ref:`infuse-vendor-raspberrypi`. A successful
build alone does not validate radio range, simultaneous network throughput,
access-point compatibility or power consumption.
