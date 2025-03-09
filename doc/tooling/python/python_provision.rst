.. _python_provision:

Hardware Provisioning
#####################

The ``provision`` subcommand is responsible for collecting the information required to
provision a piece of hardware in Infuse-IoT, providing that information to the cloud,
and flashing the result to the connected microcontroller.

For a general description of provisioning on Infuse-Iot, see :ref:`platform-provisioning`.

Running
*******

The only required argument for the tool is the SoC manufacturer (``--nrf`` or ``--stm``) so that
the appropriate programming tools can be loaded.

.. code:: bash

    infuse provision (--nrf | --stm)

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
