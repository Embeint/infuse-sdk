.. _infuse-vendor-raspberrypi:

Raspberry Pi Pico
#################

The ``infuse`` and ``usb`` snippets support Pico, Pico W, Pico 2 and Pico 2 W.
Use ``samples/low_power`` with ``-S usb`` for USB logging, or
``samples/releases/serial`` for telemetry and RPC on RP2350.
The Pico HAL is imported at the revision pinned by the SDK's Zephyr manifest.

Identity and storage
********************

Infuse first reads a provisioned ID from a dedicated flash sector. If no valid
record exists, the low 48 bits of the hardware ID are combined with Infuse's
locally managed prefix. This fallback supports USB bring-up before cloud
provisioning.

Pico and Pico W use the external flash's eight-byte identifier. Pico 2 and Pico
2 W use the RP2350's native eight-byte chip ID, with
``CONFIG_HWINFO_RPI_PICO_CHIP_ID=y`` selected by the Raspberry Pi SDK defaults. These
choices match the hardware IDs reported by ``picotool`` and sent in Infuse's
security-state challenge. The cloud SoC names are ``rp2040`` and ``rp2350``.

The HUK provider hashes the hardware ID. This provides device-specific key
material, but the identifier is readable and is not a protected hardware secret.

Both generations reserve the final 32 KiB of onboard flash for KV storage, the
preceding 4 KiB for provisioning, and 256 bytes at the start of SRAM for reboot
retention. RP2040 preserves its
256-byte second-stage bootloader; RP2350 preserves Zephyr's boot image metadata.
Application SRAM starts after the retained region, which is excluded from
startup clearing and lies away from boot-ROM scratch space.

USB provisioning
****************

Use ``infuse provision --rpi`` from the updated Python tools while the board is
in USB BOOTSEL mode; see :ref:`python_provision`. The tool uses Raspberry Pi's
``picotool`` to read the hardware ID, obtain the assigned Infuse ID from the
cloud, and write and verify the provisioning record. No debug probe is required.

Provisioning uses rewritable flash as the UICR equivalent across all four
boards. It does not burn RP2350 OTP fuses. Firmware treats the partition as
read-only, and normal UF2 updates and KV resets exclude it. A full-chip erase
or firmware using a different flash layout can erase it; rerunning provisioning
restores the cloud-assigned ID for the same hardware.

The record starts at flash offset ``0x1f7000`` on 2 MiB Pico boards and
``0x3f7000`` on 4 MiB Pico 2 boards. It contains little-endian magic
``0x49504649``, version ``1`` (both 32-bit), the 64-bit Infuse ID, and its
64-bit complement. The remainder of the sector is erased (``0xff``).
The complement detects incomplete writes and single-bit corruption. The host
refuses to overwrite an occupied, invalid or differently provisioned sector.
The parser lives in ``lib/vendor/raspberrypi/identifiers.c`` and uses the same
golden record as the Python tools.

Randomness
**********

RP2040 uses Zephyr's opt-in ``ENTROPY_RP2040_ROSC`` driver. The Infuse overlay
enables the ROSC node, and the Raspberry Pi defaults select the driver. It
supports serialized thread and busy-wait interrupt requests; non-blocking
requests return ``-EAGAIN``. ROSC is not a cryptographic entropy source:
``ENTROPY_HAS_DRIVER`` and ``CSPRNG_ENABLED`` remain disabled. Infuse defaults
disable Mbed TLS, ePacket and RPC on RP2040 to avoid the legacy Mbed TLS timer
randomness fallback. An application needs another secure entropy source before
enabling those features. USB logging, identity and storage still work.

RP2350 uses Zephyr's existing Pico entropy driver and the Pico SDK TRNG.
The Pico HAL integration places the SDK random state in ``NOINIT`` on both
generations, outside the initialized-data startup copy.

Wireless models
***************

The onboard LED on W models is CYW43439 GPIO0. It requires the wireless chip's
firmware and AIROC driver to be initialized, even without joining a network.
Use Zephyr's standard GPIO API for pin 0 on ``cyw43_gpio``, or ``gpio-leds``.
The CYW43 GPIO driver sends a mask/value pair and preserves unrelated pins.
GPIO calls must run in thread context and report unavailable hardware or
transport failures.

USB-only applications leave networking and AIROC disabled and need no wireless
firmware blobs. The serial sample continues without an unavailable LED. Enable
networking and Wi-Fi explicitly in a wireless profile, select the appropriate
firmware, and fetch it with::

   west blobs fetch hal_infineon --allow-regex '.*43439A0\.(bin|clm_blob)$'

Pico W selects CYW43439 only when AIROC is enabled; the board defconfig does not
force wireless support into USB-only builds.

Wi-Fi and Bluetooth integration
*******************************

Use :ref:`snippet-infuse-pico-wireless` to enable both wireless interfaces, or
build :ref:`sample-epacket-wireless` for USB, BLE GATT and UDP
telemetry. The snippet enables IPv4/DHCP and the existing KV-backed Wi-Fi
connection manager. USB-only builds leave the wireless chip disabled. The
onboard LED uses ``gpio-leds`` on the CYW43 GPIO controller.
RP2040 applications need another secure entropy source for Infuse security;
Pico 2 W uses its hardware TRNG.

Bluetooth uses the CYW43439 shared SPI bus rather than the UART HCI transport.
The SDK driver loads the Raspberry Pi Bluetooth patch firmware at
``bt_enable()``, shares a recursive bus mutex with WHD, and uses bounded ring
validation and stack-backed backplane transfers. The build enables shared antenna
coexistence in a generated copy of the pinned Murata NVRAM. Fetch the
additional firmware with ``west blobs fetch infuse-sdk``. The build selects
the Pico Wi-Fi firmware with ``btsdio`` support and its matching CLM. The stock
WHD Wi-Fi firmware does not initialize the Bluetooth shared-memory region.
The driver rejects a zero shared-memory base before writing ring indices,
protecting Wi-Fi firmware from an incompatible image.

.. _pico-wireless-licensing:

Firmware licensing and release notices
======================================

The shared-bus transport is derived from Raspberry Pi pico-sdk code and
retains the Raspberry Pi and Embeint copyright notices and full BSD-3-Clause
terms in its source. The firmware headers are imported from
``georgerobotics/cyw43-driver`` under its Raspberry Pi-specific ``LICENSE.RP``
grant. Keep ``SOC_FAMILY_RPI_PICO`` in the HCI driver's Kconfig dependencies:
these blobs must only be used with Raspberry Pi semiconductor devices.

For customer firmware releases containing these components, distribute
``THIRD_PARTY_NOTICES.md``, ``zephyr/blobs/LICENSE.RP`` and
``zephyr/blobs/LICENSE`` with the firmware documentation or other accompanying
materials. The notice bundle reproduces the copyright notices, conditions
and disclaimers required for binary redistribution. The upstream default
``LICENSE`` is an alternative non-commercial grant, retained to satisfy the
reference in ``LICENSE.RP``; it is not the grant selected by this integration.

These are different terms from the stock ``hal_infineon`` firmware blobs.
The selected headers carry a George Robotics notice while the controller
firmware is supplied through the CYW43 driver repository. Before making this
a product release path, the licensing owner must verify the redistribution
rights for the selected Wi-Fi/CLM and Bluetooth images, including the
upstream rights chain. The pinned upstream firmware README provides format
details but no independent firmware licence.

Power and startup limitations
=============================

The receive thread polls every 4 ms and calls into the bus wake path, keeping
the shared bus active and waking the MCU 250 times per second. Host-wake
interrupt-driven reception is a follow-up; this is not a low-power idle
implementation. The receive priority and send timeout are Kconfig options.

Bluetooth firmware download holds the shared mutex, including a 150 ms
settling delay and two ready waits of up to 300 ms each. Wi-Fi operations can
stall during this startup sequence. Enable Bluetooth before network traffic
starts. The generated Murata 1YN compatibility NVRAM retains the profile used
for hardware validation; it differs from the Pico driver's own NVRAM and
still requires product calibration/profile qualification.

Hardware validation (2026-09-30)
===============================

The results below were recorded before the generic sample rename and review
hardening. Before the entropy-policy change, sample builds covered both Pico W targets
and nRF7002 DK. Hardware validation has not been repeated for this revision.

Pico W bring-up confirmed USB startup logs, the controller public address
(Wi-Fi MAC + 1), legacy connectable advertising, Infuse GATT discovery, ATT
MTU 247, and telemetry notifications. The CYW43439 rejects extended-advertising
and extended-scan commands, so this integration uses legacy BLE and GATT.

Using the Pico shared-bus Wi-Fi firmware confirmed scanning, WPA2 association
and an IPv4 DHCP lease. Two BLE connections each negotiated ATT MTU 247 and
received three telemetry notifications while Wi-Fi stayed connected. USB echo
RPCs remained responsive, and the UDP interface opened its socket. The wireless
snippet sizes the system workqueue, network management event and socket service
stacks for these call paths. Bluetooth backplane accesses restore the chipcommon
window required by WHD power-control accesses.

Pico 2 W hardware validation also confirmed a Wi-Fi scan (20 access points),
WPA2 association, IPv4 DHCP and USB echo. On the final application, two BLE
connections each negotiated ATT MTU 247 and delivered three decrypted
``ANNOUNCE_V2`` TDF notifications while Wi-Fi stayed connected. Network settings
survived the update from the diagnostic to the normal application. Provisioning
storage was unchanged.

Pico W and Pico 2 W application builds and 19 CYW43 QEMU transport tests pass.
The platform series also has QEMU tests for ROSC entropy, CYW43 GPIO and
provisioning. Hardware validation has not been repeated after replacing the
LED driver and changing the RP2040 entropy policy.
Sustained coexistence, radio range and end-to-end UDP/cloud delivery have not yet
been verified.
