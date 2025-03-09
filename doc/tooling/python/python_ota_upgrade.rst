.. _python_ota_upgrade:

Over-the-Air Upgrades
#####################

The ``ota_upgrade`` tool monitors incoming :c:enumerator:`TDF_ANNOUNCE` packets to find devices
running old versions of a given release and automatically upgrades them. The tool will run
continuously until stopped with ``Ctrl+C``.

.. code:: bash

   > infuse ota_upgrade --help
   usage: infuse ota_upgrade [-h] --release RELEASE [--rssi RSSI]

   Automatically OTA upgrade observed devices

   options:
     -h, --help            show this help message and exit
     --release RELEASE, -r RELEASE
                           Application release to upgrade to
     --rssi RSSI           Minimum RSSI to attempt upgrade process

Release
*******

The ``--release`` argument must point to the folder of an application release generated
by the :ref:`tooling_release_framework`. For a given observed device to be upgraded, the
release folder must contain a valid diff from the current version of the device, as generated
by ``west release-diff``.

RSSI Filtering
**************

To prevent the tool from wasting time attempting to connect to devices with very low signal
strengths, a RSSI limit can be applied to connection attempts with the ``--rssi`` argument.
If provided, only devices with an RSSI at least that strong will trigger a connection attempt.
