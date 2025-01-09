.. _watchdog_api:

Watchdog Integration
####################

The Infuse-IoT watchdog wrapper integrates the Zephyr watchdog API into other Infuse-IoT
subsystems, normalises hardware capabilities, and adds new features.

The wrapper automatically uses the watchdog device from the ``watchdog0`` alias:

.. code-block:: devicetree

	aliases {
		watchdog0 = &wdog0;
	};

Integrated Subsystems
*********************

Several Infuse-IoT subsystems automatically claim watchdog channels to ensure that critical
system threads are operating normally:

   * :ref:`rpc_api`: Command processing thread
   * :ref:`epacket_api`: Packet processing thread
   * :ref:`task_runner_api`: :ref:`infuse_workqueue` thread

Software Pre-Warning
********************

For use cases where large amounts of work need to be performed on watchdog expiry, and the hardware
automatically reboots the device too quickly, the wrapper implements a software pre-warning feature,
:kconfig:option:`CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING`. This warning is implemented on top of standard
Zephyr kernel timers, and  calls :c:func:`infuse_watchdog_warning` a configurable time before the
hardware watchdog would expire.

The default behaviour of this warning is to immediately reboot the device after storing reboot reason
information.

.. note::

    This feature does not replace the hardware channels, which will still expire normally if the
    software warning callback fails to run or blocks.

Software Multi-channel Emulation
********************************

When the underlying watchdog only implements a single hardware watchdog channel, the Infuse-IoT
integration can simulate multiple hardware channels in software, enabled by
:kconfig:option:`CONFIG_INFUSE_WATCHDOG_SW_MULTICHANNEL`. This is achieved by only feeding the hardware
channel once every allocated software channel has fed the watchdog, and is transparent to watchdog
users.

API Reference
*************

.. doxygengroup:: infuse_watchdog_apis
