.. _kv_store_api:

Key-Value Store
###############

The Infuse-IoT platform implements a type layer over the top of either the
`Zephyr NVS`_ or `Zephyr ZMS`_ filesystems. Which filesystem is used by
default depends on the SoC non-volatile storage technology, for more details
see the ZMS documentation.

Callbacks
*********

Application code can be notified of changes to configuration values by
subscribing to events with :c:func:`kv_store_register_callback`.

Value Reflection
****************

Value reflection is the mechanism through which the Infuse cloud service
can detect automatically when a key value has changed on the device.

By compressing relevant KV store values into a single global CRC, the
Infuse cloud service can automatically detect when a key value has changed
on the device, triggering a re-synchronisation.

The current CRC value can be accessed through :c:func:`kv_store_reflect_crc`,
and is present in the standard :c:struct:`tdf_announce` structure.

User-defined Slots
******************

Infuse-IoT also allows custom user-defined TDFs to be integrated with the framework with
:kconfig:option:`CONFIG_INFUSE_DEFS_GENERATED_DOWNSTREAM`. See :ref:`tooling_user_definitions`
for more details.

API Reference
*************

.. doxygengroup:: kv_store_apis

.. _Zephyr NVS: https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html
.. _Zephyr ZMS: https://docs.zephyrproject.org/latest/services/storage/zms/zms.html
