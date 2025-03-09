.. _python_gateways:

Gateways
########

The python gateway tools act as a bridge between local Infuse-IoT devices
and other tools running on the computer. Currently the gateways support
interaction with devices connected directly via a serial port, or Bluetooth.

Bluetooth connectivity can be provided through native PC Bluetooth libraries
with the ``infuse native_bt`` tool, or via an embedded device with the
``infuse gateway`` tool.

Both tools are designed to run persistently in the background, managing
connections to remote devices, forwarding packets out to Infuse-IoT hardware
and broadcasting received data on localhost multicast UDP sockets.

.. note::

    Gateway tools rely on an Infuse-IoT API key and any custom network keys
    being provided through the :ref:`python_credentials` command.

Native Bluetooth
****************

Native Bluetooth support is supported through the cross platform `bleak`_ package.

.. code:: bash

    > infuse native_bt
    usage: infuse native_bt [-h]

    Use the local Bluetooth adapter for Bluetooth interaction

    options:
      -h, --help  show this help message and exit

.. note::

    On MacOS, ``native_bt`` currently relies on a `undocumented IOBluetooth API`_


Embedded Devices
****************

To create a bridge to a physically connected device, use the ``infuse gateway`` command.
This command can be used both for applications that forward data over Bluetooth
(``infuse-sdk/apps/gateway_usb``) or devices that only interact over the physical link.

An appropriate connection backend must be provided to the tool depending on the physical
connection:

.. code:: bash

    > infuse gateway --help
    usage: infuse gateway [-h] (--serial SERIAL | --rtt RTT | --pyocd PYOCD) [--display-only] [--log [filename]]

    Connect to a gateway device over serial and route commands to Bluetooth devices

    options:
      -h, --help            show this help message and exit
      --serial SERIAL       Gateway serial port
      --rtt RTT             RTT serial port
      --pyocd PYOCD         RTT via PyOCD
      --display-only, -d    No networking, only display serial
      --log [filename], -l [filename]
                            Save serial output to file

Supported backends
==================

  * ``--serial`` is used for standard serial ports (e.g. ``COMx``, ``/dev/ttyACMx``).
  * ``--rtt`` is used for RTT through `pylink`_. The argument is the device name (e.g. ``nRF52840_xxAA``)
  * ``--pyocd`` is used for RTT through `pyocd`_. The argument is the device name (e.g. ``stm32l451ceux``)

Other options
=============

By default only a single instance of the ``gateway`` tool can run at a time due to its
usage of specific UDP multicast ports. As the tool is also useful for basic log message displays,
the networking aspect of the tool can be disabled with ``--display``.

Recording of the serial logs to a dedicated file can be enabled with the ``--log`` parameter.
This can be useful for long lived monitoring that would otherwise be lost due to terminal scrollback
limits.

.. _bleak: https://github.com/hbldh/bleak
.. _undocumented IOBluetooth API: https://bleak.readthedocs.io/en/latest/backends/macos.html#bleak.backends.corebluetooth.scanner.CBScannerArgs.use_bdaddr
.. _pylink: https://github.com/square/pylink
.. _pyocd: https://github.com/pyocd/pyOCD
