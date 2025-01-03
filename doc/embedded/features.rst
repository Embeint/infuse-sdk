.. _infuse-embedded-features:

Application Features
####################

The Infuse-IoT embedded stack is architectured as a collection of software libraries which aim
to implement a large portion of the functionality required by every embedded IoT application.
Each library is written in line with the :ref:`platform-design-goals`, allowing their re-use in
any application.

  * :ref:`epacket_api`: Secure communication interface abstractions
  * :ref:`task_runner_api`: Flexible high-level task scheduling
  * :ref:`tdf_api`: Size optimised time-series data logging
  * :ref:`data_logger_api`: Block based data logging
  * :ref:`kv_store_api`: Cloud mirrored device configuration
  * :ref:`epoch_time_api`: Civil time synchronisation
  * :ref:`rpc_api`: Server & client
  * :ref:`app_states_api`: Global boolean states
  * :ref:`cpatch_api`: Binary diff application image upgrades
  * :ref:`watchdog_api`: Watchdog integration wrapper
  * :ref:`infuse_workqueue`: Non time-critical workqueue
  * :ref:`zbus_api`: Common `zbus`_ channel definitions
  * Post-deployment algorithm loading/updates (Coming soon)

.. toctree::
   :maxdepth: 1
   :hidden:
   :glob:

   features/*

.. _zbus: https://docs.zephyrproject.org/latest/services/zbus/index.html
