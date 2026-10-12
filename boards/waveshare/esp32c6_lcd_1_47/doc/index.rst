.. _board_esp32c6_lcd_1_47:

Waveshare ESP32-C6-LCD-1.47
#########################

Overview
********

The `ESP32-C6-LCD-1.47`_ has a 160 MHz RISC-V HP core, a low-power RISC-V
core, Wi-Fi, Bluetooth LE, IEEE 802.15.4, a 172 x 320 ST7789 LCD, a
WS2812 RGB LED, a microSD slot, and BOOT/RESET buttons. There is no touch
controller or IMU on this board.

The SDK extends Zephyr's board with 8 MB variants. The Infuse snippet adds
the LCD, PWM backlight, shared SD bus, RGB LED and RTC configuration.

The base target is ``esp32c6_lcd_1_47/esp32c6/hpcore`` for the documented
4 MB flash hardware. Use ``esp32c6_lcd_1_47/esp32c6/hpcore/flash8m`` for
boards with 8 MB flash. Confirm the chip and flash capacity with ``esptool
chip-id`` and ``esptool flash-id`` before choosing a target. Both variants
use Espressif's corresponding default MCUboot image-slot layout.

Hardware routing
****************

.. list-table:: Onboard connections
   :header-rows: 1
   :widths: 35 35 30

   * - Feature
     - Connection
     - Zephyr interface
   * - LCD / microSD SPI
     - SCLK 7, MOSI 6, MISO 5
     - SPI2
   * - LCD control
     - CS 14, DC 15, RESET 21
     - MIPI DBI / ST7789V
   * - LCD backlight
     - GPIO22
     - LEDC channel 0 / PWM LED
   * - SD chip select
     - GPIO4
     - SDHC SPI / disk ``SD``
   * - RGB LED
     - GPIO8
     - WS2812 UART / LED strip
   * - BOOT button
     - GPIO9, active low
     - GPIO keys / ``sw0``
   * - RESET button
     - Chip enable
     - Hardware reset
   * - USB-C
     - GPIO12/13
     - USB Serial/JTAG console
   * - UART header
     - TX 16, RX 17
     - UART0
   * - External I2C (optional)
     - SDA 0, SCL 1
     - I2C0 (disabled by default)

The LCD and SD card share SPI2, with independent GPIO chip selects managed
by the SPI driver. The LCD column offset is 34 pixels. Its initialization
parameters follow Waveshare's demo, including its final horizontal scan
direction setting (MADCTL MX). Keep the backlight at 50 percent or
lower, as recommended by Waveshare to avoid overheating the display.
The LEDC ``sleep-keep-alive`` option preserves PWM through CPU light sleep;
this is provided by the pinned Zephyr branch. It retains
the XTAL clock, LEDC and IOMUX, so it increases light-sleep power compared
with switching the backlight off.

UART1 and a GDMA transmit channel generate the WS2812 signal with the
existing UART LED-strip driver. This leaves SPI2 available for LCD and SD
traffic. Enabling ``LED_STRIP`` selects the asynchronous UART API.
UART0 ePackets use the interrupt-driven backend so UART0 does not compete
with the RGB LED for UHCI. The byte-stream transmit option is enabled
because the ESP32 driver reports FIFO readiness as a boolean.

The header exposes GPIO0..5, GPIO9, GPIO18..20, GPIO23, UART0, power, and
USB signals. GPIO4/5 are also used by microSD. GPIO9 is a boot strap and
the BOOT button. Do not repurpose those shared pins while their onboard
function is in use. GPIO0..3 can be used with the SoC ADC driver; ADC
channel configuration is an application choice. The on-chip temperature
sensor is also available.

The RTC counter is enabled as a wakeup source for light sleep. Infuse's
system and runtime-device power management defaults are retained. USB
Serial/JTAG communication stops during light sleep; the generic Infuse display
sample disables the USB console and runs independently. Console and radio
examples restrict system sleep states while leaving the PM framework and
runtime-device PM enabled. The pinned radio drivers do not coordinate
automatic light sleep with Zephyr's PM policy.

With Infuse enabled, shell TX uses the USB UART driver's bounded polling
path. The interrupt-driven shell TX path can wait indefinitely when the
host closes its serial reader, including inside Bluetooth shell connection
callbacks. Bounded TX allows BLE to keep processing without an open serial
monitor; console output may be dropped while the host is not reading.

Infuse integration
******************

Use ``-S infuse`` for Bluetooth advertising/GATT ePackets, UART0 ePackets,
optional Wi-Fi UDP ePackets, key-value storage in the storage partition,
and reboot metadata in the SoC's reserved LP retention memory. USB is the
human console, while the UART0 header carries binary ePackets.

The hardware ID is the factory base MAC. A cloud-provisioned 64-bit Infuse
ID is read from USER_DATA eFuse bits 0..63. Blank eFuses select the locally
managed ``0xffff`` MAC namespace. See :ref:`python_provision` for the
``infuse provision --esp32c6 --port <serial-port>`` workflow.

The default hardware unique key provider is the
SDK's hardware-ID hash fallback; production key storage is a separate
provisioning decision.

Wi-Fi is enabled when the application enables networking. Bluetooth uses
Espressif's controller with extended advertising; Zephyr-specific vendor
HCI commands are disabled for that controller. IEEE 802.15.4 is available
through the existing Zephyr ESP32 driver. The manifest does not import
OpenThread: a Thread application must explicitly add that optional dependency.
Concurrent use of all three radios is subject to the
Espressif controller/coexistence capabilities.

Building and flashing
*********************

The SDK pins the Embeint Zephyr ``fix/esp32c6-board-ledc-4.4.1`` branch at
a fixed commit based on Zephyr 4.4.1, which includes the upstream board and
LEDC light-sleep support. After ``west update``, install the Zephyr and
Espressif Python requirements and fetch ``west blobs fetch hal_espressif``.
Use a Zephyr SDK with the
RISC-V toolchain. From the workspace root:

.. code-block:: console

   west build --sysbuild -b esp32c6_lcd_1_47/esp32c6/hpcore/flash8m -S infuse \
       infuse-sdk/samples/display -d build/waveshare-display
   west flash -d build/waveshare-display --esp-device /dev/cu.usbmodem11301

The sample enables Infuse's existing sysbuild integration and uses
the board's ECDSA P-256 signature configuration. Its bundled test signing key
is for development; provide a private ``SB_CONFIG_BOOT_SIGNATURE_KEY_FILE``
for production builds.
Choose the 4 MB target when appropriate; the 8 MB layout is not safe for
a 4 MB device. Flashing replaces the firmware, so preserve an original
flash dump if the vendor demo needs to be restored.

If a board was manually placed in ROM download mode with BOOT/RESET and
the console still reports ``waiting for download`` after flashing, tap
RESET without holding BOOT to start the application.

The ``esp32c6_lcd_1_47/esp32c6/lpcore`` target (or ``lpcore/flash8m`` for
8 MB hardware) supports a separate LP-core image using the SoC's existing
Zephyr LP-core support. Match its flash variant to the HP core. That core does not
run the Infuse LCD application. LP UART uses GPIO4/5, shared with the SD
card, so the HP application's SD function must be disabled when using
that UART externally.

USB JTAG debugging uses Espressif's OpenOCD distribution. Set
``ESPRESSIF_TOOLCHAIN_PATH`` to its installation before running
``west debug``. The display sample disables USB in the application; BOOT/RESET
may be required to re-enter ROM download mode for subsequent flashing.

Generic peripheral samples
**************************

Use the existing samples to exercise each interface:

.. list-table:: Sample coverage
   :header-rows: 1

   * - Feature
     - Application
   * - Infuse identity / LCD or e-paper
     - ``infuse-sdk/samples/display``
   * - Infuse BLE advertising, GATT and authenticated RPCs
     - ``infuse-sdk/samples/epacket/bluetooth``
   * - Wi-Fi configuration, connection state and UDP/TCP throughput
     - :ref:`embedded_sample_net_zperf`
   * - SD filesystem
     - ``zephyr/samples/subsys/fs/fs_sample``
   * - RGB LED
     - ``zephyr/samples/drivers/led/led_strip``
   * - BOOT input events
     - ``zephyr/samples/subsys/input/input_dump``
   * - Display driver drawing / LVGL widgets
     - ``zephyr/samples/drivers/display`` / ``zephyr/samples/subsys/display/lvgl``

The board provides routing and aliases; no combined board application is
required. The Infuse display application sleeps between refreshes and selects
an LCD or e-paper refresh interval through ``SCREEN_INFO_EPD``. The Bluetooth
application uses the existing announce/RPC implementation. The network sample
uses stored Wi-Fi credentials and supports configuration/status RPCs over
Bluetooth, with ``zperf_upload`` for throughput checks.

The console-based Zephyr examples need an awake CPU while using USB Serial/JTAG.
For these checks, create ``console.overlay`` containing:

.. code-block:: dts

   &cpu0 {
       /delete-property/ cpu-power-states;
   };

Then build the SD example with read-only FAT/exFAT/GPT support::

   west build --sysbuild -b esp32c6_lcd_1_47/esp32c6/hpcore/flash8m -S infuse \
       zephyr/samples/subsys/fs/fs_sample -d build/c6-sd -- \
       -DSB_CONFIG_BOOTLOADER_MCUBOOT=y \
       -DEXTRA_DTC_OVERLAY_FILE=/absolute/path/to/console.overlay \
       -DCONFIG_PM=y -DCONFIG_PM_DEVICE=y -DCONFIG_PM_DEVICE_RUNTIME=y \
       -DCONFIG_COUNTER=y -DCONFIG_FS_FATFS_READ_ONLY=y \
       -DCONFIG_FS_FATFS_MOUNT_MKFS=n -DCONFIG_FS_FATFS_MKFS=n \
       -DCONFIG_FS_SAMPLE_CREATE_SOME_ENTRIES=n \
       -DCONFIG_FS_FATFS_EXFAT=y -DCONFIG_FS_FATFS_LBA64=y

These filesystem options prevent formatting and creating files on the inserted
card. For LED-strip or input checks, substitute the corresponding sample path
and omit the filesystem options. Keep ``--sysbuild`` and MCUboot enabled to
support signed firmware updates. These separate samples do not demonstrate
concurrent LCD/SD traffic; that needs a dedicated integration test.

Wi-Fi association, BLE connections and IEEE 802.15.4 peer tests need a suitable
peer/network; the board definition alone is not evidence of end-to-end radio
validation. Enabling PM is likewise not a current-consumption measurement.

References
**********

* `ESP32-C6-LCD-1.47`_
* `Waveshare resources and schematic`_
* `Waveshare demo archive`_

.. _ESP32-C6-LCD-1.47: https://docs.waveshare.com/ESP32-C6-LCD-1.47
.. _Waveshare resources and schematic: https://docs.waveshare.com/ESP32-C6-LCD-1.47/Resources-And-Documents
.. _Waveshare demo archive: https://files.waveshare.com/wiki/ESP32-C6-LCD-1.47/ESP32-C6-LCD-1.47-Demo.zip
