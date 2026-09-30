.. _snippet-infuse-pico-wireless:

Pico wireless support (infuse-pico-wireless)
##########################################

Use this snippet with ``infuse`` on ``rpi_pico/rp2040/w`` or
``rpi_pico2/rp2350a/m33/w``. It enables the AIROC Wi-Fi driver, native IPv4,
DHCP, Infuse KV-backed connection management, and CYW43439 Bluetooth HCI over
the shared SPI bus. It adds ePacket GATT and UDP interfaces.
Applications can also use Zephyr's normal Bluetooth and Wi-Fi APIs.
The controller uses legacy LE advertising/scanning. Applications must start
connectable advertising to expose GATT; the sample demonstrates this. Infuse
ePacket advertising telemetry requires extended advertising and is disabled.

Fetch firmware before building::

   west blobs fetch hal_infineon --allow-regex '.*43439A0\.(bin|clm_blob)$'
   west blobs fetch infuse-sdk
   west build -b rpi_pico/rp2040/w -S infuse -S infuse-pico-wireless <application>

See :ref:`sample-epacket-pico-wireless` for a complete USB/BLE/Wi-Fi example.
The USB-only sample retains its original configuration.

Bluetooth requires the initialized Wi-Fi driver even when there are no network
credentials. The shared-bus HCI driver loads the Bluetooth patch at
``bt_enable()``, serializes backplane accesses with Wi-Fi, and polls the
controller's receive ring. Bluetooth transfers use stack buffers independently
of the Wi-Fi packet pool. The build selects the Pico Wi-Fi firmware with ``btsdio`` support and its
matching CLM, plus the Bluetooth patch. The stock WHD Wi-Fi firmware lacks
the shared-memory setup required by Bluetooth. Both blobs are fetched at a
pinned revision with SHA-256 checks; their Raspberry Pi license is in
``zephyr/blobs/LICENSE.cyw43`` and permits use on Raspberry Pi semiconductor
parts. No alternate Bluetooth host stack is included.

The pinned Murata NVRAM profile disables Bluetooth coexistence. The driver build
generates a local copy with ``btc_mode=1`` and ``muxenab=0x100`` for the shared
antenna. Imported Zephyr and HAL repositories are unchanged. This integration
uses the pinned WHD internals and linker wrappers; recheck arbitration and
NVRAM generation when upgrading Zephyr or the Infineon HAL.

The pinned AIROC buffer-release callback bypasses ``net_buf_unref()``, causing
available-buffer accounting to become negative with ``NET_BUF_POOL_USAGE``.
The Infuse Wi-Fi build substitutes a corrected local copy of that callback;
imported sources remain unchanged.

The snippet increases the system workqueue, network-management event and socket
service stacks for association, WHD status queries and ePacket receive callbacks.
It enables native socket send timeouts for the UDP interface.

The HCI transport supports commands, events and ACL data for BLE. SCO/audio,
ISO and Bluetooth Classic are not supported. Continuous Wi-Fi/BLE coexistence
and power consumption require hardware validation for the intended application.
