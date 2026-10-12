.. _python_provision:

Hardware Provisioning
#####################

The ``provision`` subcommand is responsible for collecting the information required to
provision a piece of hardware in Infuse-IoT, providing that information to the cloud,
and flashing the result to the connected microcontroller.

For a general description of provisioning on Infuse-Iot, see :ref:`platform-provisioning`.

Running
*******

The only required argument for the tool is the SoC manufacturer (``--nrf``, ``--stm`` or ``--rpi``) so that
the appropriate programming tools can be loaded.

.. code:: bash

    infuse provision (--nrf | --stm | --rpi)

By default, Infuse-IoT cloud will generate a random Infuse ID for the device when it is
first provisioned. If a specific Infuse ID is desired, it can be provided through the ``--id``
parameter.

.. code:: bash

    infuse provision --nrf --id 0xaa00000000000001

.. warning::

    Once provisioned, the Infuse ID is permanently assigned to that specific piece of
    hardware (enforced through internal SoC identifiers) and cannot be changed.

If the piece of hardware has not previously been provisioned to Infuse-IoT, the tool will
automatically populate a list of organisation and board IDs that the hardware can be provisioned
as. If already known, these can be provided as command line arguments.

.. code:: bash

    infuse provision --nrf --organisation 413c1966-9186-40da-b412-590afb10c301 --board 2a9fc6b7-d8d4-4fea-9a16-1790e0aa8c63

If the hardware already exists in Infuse-IoT, the existing provisioning information will be
re-flashed to the hardware.

Use ``--dry-run`` to inspect the provisioning request without writing cloud records
or flash. Dry runs still query cloud and hardware state, but do not reset the target.

Raspberry Pi Pico over USB
**************************

Install `picotool <https://github.com/raspberrypi/picotool>`_ 2.3.1 or later and
make it available on ``PATH``. On macOS, ``brew install picotool`` installs it.
On Linux, install its USB access rules as described in the picotool documentation.

Build and flash firmware using the Infuse Pico flash layout, then reconnect
while holding BOOTSEL. Run::

    infuse provision --rpi --organisation <organisation-uuid> --board <board-uuid>

The cloud board's SoC must be ``rp2040`` for Pico/Pico W, or ``rp2350`` for
Pico 2/Pico 2 W. The usual API credentials and optional ``--id`` and
``--metadata`` arguments apply. If multiple boards are connected, select the
intended one using the serial number reported by ``picotool info -d``::

    infuse provision --rpi --usb-serial E66430A64B581E32

The tool refuses ambiguous devices, unexpected flash sizes, conflicting IDs and
nonempty invalid provisioning sectors. It verifies both the flash write and a
separate readback before rebooting. A dry run writes neither cloud records nor
flash; it still performs read-only cloud and hardware queries. A dry run or an
already-matching ID leaves the Pico in BOOTSEL; use ``picotool reboot`` to run
the application.

Provisioned IDs live in a dedicated 4 KiB flash sector, separate from the
application and KV storage. They survive normal UF2 updates, power loss and KV
resets. This is not physically one-time-programmable storage; a full flash erase
removes the local copy. See :ref:`infuse-vendor-raspberrypi` for the layout.
