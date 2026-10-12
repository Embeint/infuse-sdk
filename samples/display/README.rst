Infuse display
##############

This sample displays the Infuse device identity and uptime through Zephyr's
``zephyr,display`` device and LVGL. It has no Waveshare runtime branches and
does not require an RGB LED, SD card, button, Wi-Fi, Bluetooth or USB monitor.
An optional ``display-backlight`` alias supplies a backlight at 25 percent brightness.
Boards using that alias enable ``LED`` and ``PWM`` in their sample configuration;
panels without a backlight do not need those drivers.

The same application can use LCD and e-paper drivers. LCDs update once per
second; panels reporting ``SCREEN_INFO_EPD`` update once per minute. Driver
configuration supplies the resolution, pixel format and transfer alignment.
For monochrome panels, use the LVGL colour-depth and alignment configuration
required by the chosen display driver. E-paper hardware has not been tested
with this sample.

Infuse's system and runtime-device power management defaults remain enabled.
The application sleeps between refreshes. The C6 configuration uses the RTC
timer to resume from light sleep and disables the USB console/controller.
It does not enable radios: the pinned C6 radio drivers do not coordinate
automatic light sleep with Zephyr's PM policy. Neither current consumption
nor continuous backlight output is implied by enabling PM. The C6 board uses
the companion Zephyr LEDC ``sleep-keep-alive`` driver option to preserve its
25-percent PWM during light sleep, retaining XTAL at a power cost.

The manifest pins the Embeint Zephyr branch containing LEDC light-sleep
support; ``west update`` supplies the driver and binding.
Build for the connected 8 MB board:

.. code-block:: console

   west build --sysbuild -b esp32c6_lcd_1_47/esp32c6/hpcore/flash8m -S infuse \
       infuse-sdk/samples/display -d build/waveshare-display
   west flash -d build/waveshare-display --esp-device /dev/cu.usbmodem11301

The 4 MB target omits ``/flash8m``. Both use MCUboot for signed firmware updates.
With USB disabled in the application, BOOT/RESET may be needed to enter ROM
download mode for another flash. The display is the sample's visible output.

Other peripherals use separate, existing samples: ``samples/epacket/bluetooth``
for Infuse advertising/GATT and RPCs, and Zephyr's ``samples/subsys/fs/fs_sample``,
``samples/drivers/led/led_strip`` and ``samples/subsys/input/input_dump``.
