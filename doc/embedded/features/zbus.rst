.. _zbus_api:

Common zbus Channels
####################

Infuse-IoT defines a number of common zbus channels to enable passing common
data between various software modules. These channels are defined with an
identifier (:c:enum:`infuse_zbus_channel_id`).

Accessing Common Channels
*************************

Infuse-IoT provides several convenience macros for accessing the common channels.
Before referencing a common channel, it must be declared using :c:macro:`INFUSE_ZBUS_CHAN_DECLARE`
together with a list of :c:enum:`infuse_zbus_channel_id` identifiers. Once declared, a pointer
to the channel can be obtained with :c:macro:`INFUSE_ZBUS_CHAN_GET`, and the type of the channel
is retrieved through :c:macro:`INFUSE_ZBUS_TYPE`.

Once a channel pointer has been obtained, all standard zbus APIs can be used.

.. code-block:: c

   INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_BATTERY, INFUSE_ZBUS_CHAN_AMBIENT_ENV);

   void print_system_battery(void)
   {
      INFUSE_ZBUS_TYPE(INFUSE_ZBUS_CHAN_BATTERY) battery;

      zbus_chan_read(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY), &battery, K_FOREVER);
      LOG_INF("Battery Voltage: %d mV Current: %d uA Charge: %d %%", battery.voltage_mv, battery.current_ua, battery.soc);
   }

API Reference
*************

.. doxygengroup:: infuse_zbus_channels_apis
