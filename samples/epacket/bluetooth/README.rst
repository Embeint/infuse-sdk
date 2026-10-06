Bluetooth advertising and GATT
##############################

This sample publishes Infuse announce packets over the existing advertising
and GATT ePacket interfaces. The standard authenticated RPC service is enabled
by Infuse's defaults. No display, SD card, RGB LED or button is required.

Build with ``-S infuse`` and ``--sysbuild`` on boards requiring MCUboot, for example::

   west build --sysbuild -b esp32c6_lcd_1_47/esp32c6/hpcore/flash8m -S infuse \
       infuse-sdk/samples/epacket/bluetooth -d build/c6-bluetooth

The C6 configuration keeps the PM framework and device PM enabled, but excludes
automatic system sleep because the pinned controller driver does not coordinate
Bluetooth wakeup with Zephyr's light-sleep policy. This is a driver limitation.
The sample's behavior is independent of an attached serial monitor.

On the host, run ``infuse native_bt`` and then ``infuse tdf_list --id <device-id>``
or ``infuse rpc --id <device-id> application_info`` in another terminal.

For Wi-Fi association and network throughput testing, use
:ref:`embedded_sample_net_zperf`. It connects using stored credentials and
supports configuration and status RPCs through the same Bluetooth transport.
