.. _infuse-embedded-vendor-silabs:

Silicon Labs
############

General
*******

Silicon Labs Series 2 SoCs are supported, currently the `EFR32MG24`_ on the
`xG24 Dev Kit`_ (``xg24_dk2601b``).

Devices are programmed with ``west flash`` through either a `SEGGER J-Link`_ probe (default) or
`Simplicity Commander`_ (``west flash -r silabs_commander``). The J-Link software must be
installed for provisioning.

Provisioning
************

Silicon Labs provisioning information is stored at the start of the ``USERDATA`` flash page.
Unlike the OTP regions on other vendors, this page is erasable flash, and can be re-provisioned.
Devices are provisioned through a J-Link probe with ``infuse provision --silabs``.

Unprovisioned devices use a locally managed device ID derived from the factory programmed
EUI-48 address.

Debug Port Disabling
********************

:kconfig:option:`CONFIG_INFUSE_COMMON_BOOT_DEBUG_PORT_DISABLE` applies the Secure Engine
debug lock on boot, then reboots the device so that the lock is in effect.

The lock is recoverable, as device erase is left enabled. A device erase (for example
``commander device recover``) erases the main flash and unlocks the debug port.
The ``USERDATA`` page is not erased, so the device does not need to be re-provisioned.
Permanently locking the device (disabling device erase) and secure debug unlock are not
supported.

Bootloader
**********

When building with Infuse-IoT sysbuild, MCUboot images are signed with ECDSA P-256 by default
(``SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256``) instead of the RSA-2048 MCUboot default, as an
RSA bootloader does not fit in the boot partition of the supported development kits.
The bootloader does not use external flash, which is disabled in bootloader builds.

Power Management
****************

The SoC support defaults :kconfig:option:`CONFIG_PM` to disabled, in which case the core
only enters EM1 when idle. Infuse-IoT enables :kconfig:option:`CONFIG_PM` by default so that the
core can enter EM2.

Hardware Peripherals
********************

Serial
======

:kconfig:option:`CONFIG_UART_ASYNC_API` is enabled by default for the USART and EUSART drivers.
The asynchronous API requires DMA channels to be assigned to the UART in devicetree.

Watchdog
========

The Silicon Labs watchdog only supports a single hardware channel, and cannot run an expiry
callback when the watchdog resets the SoC. Infuse-IoT therefore enables
:kconfig:option:`CONFIG_INFUSE_WATCHDOG_SW_MULTICHANNEL` and
:kconfig:option:`CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING` by default.

The hardware period is rounded up to the next supported value, a power of two cycles of the
approximately 1 kHz ULFRCO. The hardware watchdog therefore expires later than
:kconfig:option:`CONFIG_INFUSE_WATCHDOG_PERIOD_MS` (65.5 seconds for the default 35 seconds),
while the software warning still uses the configured period.

.. _EFR32MG24: https://www.silabs.com/wireless/zigbee/efr32mg24-series-2-socs
.. _xG24 Dev Kit: https://www.silabs.com/development-tools/wireless/efr32xg24-dev-kit
.. _SEGGER J-Link: https://www.segger.com/downloads/jlink/
.. _Simplicity Commander: https://www.silabs.com/developer-tools/simplicity-studio/simplicity-commander
