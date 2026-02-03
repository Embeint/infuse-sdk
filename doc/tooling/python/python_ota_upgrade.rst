.. _python_ota_upgrade:

Over-the-Air Upgrades
#####################

The ``ota_upgrade`` tool monitors incoming :c:enumerator:`TDF_ANNOUNCE` packets to find devices
running old versions of a given release and automatically upgrades them. The tool will run
continuously until stopped with ``Ctrl+C``.

.. code:: bash

   > infuse ota_upgrade --help
   usage: infuse ota_upgrade [-h] (--release RELEASE | --single SINGLE) [--rssi RSSI] [--log LOG]
                             [--conn-timeout CONN_TIMEOUT] [--id ID | --list LIST]

   Automatically OTA upgrade observed devices

   options:
     -h, --help            show this help message and exit
     --release RELEASE, -r RELEASE
                           Application release to upgrade to
     --single SINGLE       Single diff
     --rssi RSSI           Minimum RSSI to attempt upgrade process
     --log LOG             File to write upgrade results to
     --conn-timeout CONN_TIMEOUT
                           Timeout to wait for a connection to the device (ms)
     --id ID               Single device to upgrade
     --list LIST           File containing a list of IDs to upgrade

Release
*******

The ``--release`` argument must point to the folder of an application release generated
by the :ref:`tooling_release_framework`. For a given observed device to be upgraded, the
release folder must contain a valid diff from the current version of the device, as generated
by ``west release-diff``.

Alternatively, a single diff file can be explicitly provided through the ``--single`` argument.
If provided, the diff file is saved onto the flash chip of the connected gateway and the gateway
itself handles the diff transfer process (the python script still controls which devices are
upgraded). Sending the diff from the gateways flash can dramatically speed up OTA times due to
eliminating the round-trip delays incurred by sending commands and responses over the serial link
and through the python tooling.

RSSI Filtering
**************

To prevent the tool from wasting time attempting to connect to devices with very low signal
strengths, a RSSI limit can be applied to connection attempts with the ``--rssi`` argument.
If provided, only devices with an RSSI at least that strong will trigger a connection attempt.

Device Filtering
****************

If only a subset of devices should be updated, either a single device can be specified with ``--id``,
or a file containing one Infuse ID per line can be provided with ``--list``. Example file contents are
provided below. For both of these arguments, the script will terminate once all specified devices have
been updated.

.. code-block:: text

    0xcc00000000100001
    0xcc00000000100002
    0xcc00000000100006
    0xcc00000000100007

Logging
*******

A CSV log of upgrade attempts and results can be created with the ``--log`` argument.
