.. _trusted_firmware_m:

Trusted Firmware-M (TF-M)
#########################

Infuse-IoT implements a number of extensions to the default functionality
of Zephyr's `Trusted Firmware-M`_ integration.

Dynamic Partition Layout
************************

Unlike standard TF-M, applications and boards are free to shift and re-size
the FLASH and RAM partitioning of the application. This applies to both the
overall (Secure + Non-Secure) and individual (Secure vs Non-Secure)
partitioning. This is all achieved through standard devicetree descriptions,
see :dtcompatible:`arm,trusted-firmware-m`.

This feature is currently limited to Nordic Semiconductor SoCs.

External Secondary Images
*************************

Due to the dynamic partition layout, TF-M applications are no longer restricted
to the internal SoC FLASH. This enables larger application image slots for
resource heavy applications. For example the total size of ``slot0`` on a
``nRF9160DK`` can increase from ``472 kB`` to ``896 kB``.

Supported external flash configurations:
  * QSPI flash
  * SPI NOR flash

Secure Fault Tracking
*********************

Infuse-IoT implements a TF-M service that stores secure fault information
across reboots. The fault information is then automatically extracted from the
secure application on the next boot and made available to the standard reboot
infrastructure (:ref:`reboot_api`).

.. note::

    Secure fault tracking does not currently preserve the time across reboots.

.. _Trusted Firmware-M: https://www.trustedfirmware.org/projects/tf-m/
