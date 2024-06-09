.. _kv_store_api:

Key-Value Store
###############

The Infuse-IoT platform implements a type layer over the top of the
`Zephyr NVS`_ filesystem.

Callbacks
*********

Application code can be notified of changes to configuration values by
subscribing to events with :c:func:`kv_store_register_callback`.

Value Reflection
****************

Value synchronisation between embedded devices and the cloud.

.. note::
  Coming soon!

API Reference
*************

.. doxygengroup:: kv_store_apis

.. _Zephyr NVS: https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html
