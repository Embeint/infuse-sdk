.. _infuse-vendor-raspberrypi:

Raspberry Pi Pico
#################

The ``infuse`` snippet supports Pico, Pico W, Pico 2 and Pico 2 W.
Use ``samples/low_power`` for idle-power validation on all four boards.
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
``ENTROPY_HAS_DRIVER`` and ``CSPRNG_ENABLED`` remain disabled.

.. warning::

   Infuse keeps Mbed TLS, ePacket, data logging and RPC enabled on RP2040.
   Without an additional secure entropy source, Mbed TLS uses its legacy DRBG
   seeded through Zephyr's insecure timer-based test RNG. The ROSC driver is
   available through the entropy API, but does not seed this fallback.
   Encryption and RPC work, but generated keys and protocol randomness cannot
   be considered cryptographically secure. Builds warn about this limitation;
   add a secure entropy source for applications that need security guarantees.

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

Applications that leave networking and AIROC disabled need no wireless firmware
blobs. Enable networking and Wi-Fi explicitly in a wireless profile, select the
appropriate firmware, and fetch it with::

   west blobs fetch hal_infineon --allow-regex '.*43439A0\.(bin|clm_blob)$'

Pico W selects CYW43439 only when AIROC is enabled; the board defconfig does not
force wireless support into USB-only builds.
