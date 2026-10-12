.. _embedded_sample_net_zperf:

Wi-Fi connectivity and throughput
################################

This sample uses Infuse's connection manager to connect to a stored Wi-Fi
network and obtain an IPv4 address through DHCP. Authenticated RPCs configure
credentials, scan for networks, inspect connection state and measure UDP/TCP
upload throughput with ``zperf``. The application also works with other
supported network interfaces.

Build and connect
*****************

Build with the Infuse snippet. For example, on the Waveshare C6 8 MB board::

   west blobs fetch hal_espressif
   west build --sysbuild -b esp32c6_lcd_1_47/esp32c6/hpcore/flash8m -S infuse \
       infuse-sdk/samples/net/zperf -d build/c6-wifi
   west flash -d build/c6-wifi

The C6 sample retains device PM but excludes automatic CPU light sleep because
the pinned radio driver does not coordinate wakeup. An attached USB console is
optional; connection and RPC handling continue without a serial monitor.
The 4 MB C6 target and ``nrf7002dk/nrf5340/cpuapp`` also have build coverage.

On Bluetooth-capable boards, the sample periodically advertises Infuse key
identifiers so ``infuse native_bt`` can discover the device and carry RPCs over
GATT. Run that host tool in one terminal. The host needs the device's matching
Infuse network and device keys. Use the advertised Infuse ID in another terminal::

   infuse rpc --id <device-id> wifi_scan
   infuse rpc --id <device-id> wifi_configure --ssid "<ssid>" --psk "<password>"
   infuse rpc --id <device-id> wifi_state

The C6 supports 2.4 GHz Wi-Fi. Credentials are kept in the existing KV store;
writing them triggers connection and later boots reconnect automatically.
Wait for ``wifi_state`` to report an associated network and an IPv4 address
before throughput testing. Change credentials with ``wifi_configure`` or remove
them with ``wifi_configure --delete``. KV storage survives firmware updates that
preserve its partition; a full flash erase removes it.

Throughput testing
******************

Run an ``iperf`` version 2 server on a computer reachable from the device's
network. Replace the example address with that computer's IPv4 address.
These RPCs remain on the Bluetooth connection while traffic uses Wi-Fi.

For UDP, start the server::

   iperf --server --interval 1 --udp

Then request an upload lasting two seconds at 1000 kbit/s::

   infuse rpc --id <device-id> zperf_upload --udp --address "192.168.20.78" --duration 2000 --rate-kbps 1000

For TCP, start the server::

   iperf --server --interval 1

Then request the upload::

   infuse rpc --id <device-id> zperf_upload --tcp --address "192.168.20.78" --duration 2000 --rate-kbps 1000

For a serial gateway, use ``--gateway`` in place of ``--id <device-id>``.
