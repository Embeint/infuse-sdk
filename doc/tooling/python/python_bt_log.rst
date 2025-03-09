.. _python_bt_log:

Bluetooth Log Viewer
####################

When :kconfig:option:`CONFIG_LOG_BACKEND_EPACKET_BT` is enabled, `Zephyr logging`_ output
is made available over a Bluetooth GATT link through :ref:`epacket_api`.

The ``infuse bt_log`` command initiates a Bluetooth connection to the requested device and
subscribes to the appropriate GATT characteristic. Once connected, the tool displays the
log messages until the tool is terminated with ``Ctrl-C``.

.. code:: bash

    infuse bt_log --id 0x00000000000003ea

.. _Zephyr logging: https://docs.zephyrproject.org/latest/services/logging/index.html
