.. _app_states_api:

Application States
##################

The application states API is a mechanism for collecting global boolean
state into a single location such that is can be accessed through a single
API and used by the :ref:`task_runner_api`.

Boolean State Timeout
*********************

In addition to basic boolean state setting and querying, the application state
API also allows for setting a state to ``true`` for a given number of seconds
through :c:func:`infuse_state_set_timeout`. The maximum number of timeouts that
can be active at one time is configured with
:kconfig:option:`CONFIG_INFUSE_APPLICATION_STATES_MAX_TIMEOUTS`.

.. note::

    Calling :c:func:`infuse_state_set` or :c:func:`infuse_state_set_to` on a
    state that has a pending timeout will cancel that timeout.

Iterating the state timeouts is performed by :c:func:`infuse_states_tick`, which
is called automatically by :c:func:`task_runner_start_auto_iterate`.

API Reference
*************

.. doxygengroup:: states_apis
