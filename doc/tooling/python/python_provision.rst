.. _python_provision:

Hardware Provisioning
#####################

The ``provision`` subcommand is responsible for collecting the information required to
provision a piece of hardware in Infuse-IoT, providing that information to the cloud,
and flashing the result to the connected microcontroller.

For a general description of provisioning on Infuse-Iot, see :ref:`platform-provisioning`.

Running
*******

The only required argument for the tool is the SoC manufacturer (``--nrf``, ``--stm`` or ``--esp32c6``) so that
the appropriate programming tools can be loaded.

.. code:: bash

    infuse provision (--nrf | --stm | --esp32c6)

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

ESP32-C6
********

Install ``infuse_iot[provisioning]`` and connect the C6 in ROM download mode.
The tool reads the factory MAC as the hardware ID and programs the cloud's
64-bit Infuse ID into USER_DATA eFuses. Existing matching IDs are left intact;
conflicting IDs or nonblank USER_DATA blocks are rejected. ``espefuse`` asks
for confirmation before the permanent write, and the result is read back.

.. code:: bash

    infuse provision --esp32c6 --port /dev/cu.usbmodem11301 \
        --organisation <organisation-uuid> --board <board-uuid> --dry-run

Remove ``--dry-run`` to provision. Use ``--id`` to request a specific Infuse
ID; hardware already registered in the cloud retains its existing ID.
``--dry-run`` performs no cloud creation or eFuse writes.
