.. _reboot_api:

Reboot Integration
##################

The Infuse-IoT reboot integration primary purpose is to preserve application state across reboots
so that relevant information can be recovered at the next boot. The reboot integration builds upon
the Zephyr `Retention API`_ to achieve this.

Reboot State Storage
********************

The reboot integration is dependent on all reboots being routed through either :c:func:`infuse_reboot`
or :c:func:`infuse_reboot_delayed`.

Reboot State Querying
*********************

Application software and libraries can query the reason for the previous reboot at any point in
time through :c:func:`infuse_common_boot_last_reboot`.

Time Recovery
*************

Global :ref:`epoch_time_api` knowledge is preserved across reboots, subject to minor drift caused
by the delay between storing the current time and the kernel tick counter restarting on the next
boot.

API Reference
*************

.. doxygengroup:: infuse_reboot_apis
.. doxygengroup:: infuse_common_boot_apis

.. _Retention API: https://docs.zephyrproject.org/latest/services/retention/index.html
