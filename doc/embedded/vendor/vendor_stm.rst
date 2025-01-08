.. _infuse-embedded-vendor-stm:

ST Microelectronics
###################

General
*******

Infuse-IoT tooling expects the `STM32CubeProgrammer`_ tooling to be installed, with
the provided binaries available on ``$PATH``.

Provisioning
************

STM provisioning information is stored in true One-Time-Programmable (OTP) memory.
This means that once provisioned, the information cannot be erased.

Debug Port Disabling
********************

STM microcontrollers do not provide a mechanism to completely disable the debug
access port (DAP) for releases. Application protection is instead limited to
ROM read-back protection (RDP), which is enabled with
:kconfig:option:`CONFIG_INFUSE_COMMON_BOOT_DEBUG_PORT_DISABLE`. The integrated
implementation only enables level 1 protection, which can be disabled by a debugger
(triggering a mass ROM erase).

A device with RDP enabled can be recovered with the following command:

.. code-block:: bash

    STM32_Programmer_CLI --connect port=SWD -rdu

.. note::

    It may take several runs of the above command to successfully disable RDP.


Hardware Peripherals
********************

Watchdog
========

The standard STM32 IWDG peripheral only supports a single hardware channel.
:kconfig:option:`CONFIG_INFUSE_WATCHDOG_SW_MULTICHANNEL` must be enabled for
proper operation of Infuse-IoT subsystems.

.. _ST Microelectronics: https://www.st.com
.. _STM32CubeProgrammer: https://www.st.com/en/development-tools/stm32cubeprog.html
