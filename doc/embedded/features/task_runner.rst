.. _task_runner_api:

Task Runner
###########

The Infuse-IoT Task Runner is a library designed for high-level
task scheduling based on the current application state.

The task runner consists of a core scheduling loop, which determines when
tasks should be started or terminated, and individual tasks, which perform
some action when scheduled.

In a typical Infuse-IoT application, the Task Runner is the driver of the
majority of the application behaviour. Combined with builtin task implementations
for the most common application actions, the Task Runner allows the basis of new
applications to be created in extremely small amounts of code.

Task Scheduling
***************

Tasks are scheduled based on the evaluation of individual :c:struct:`task_schedule`'s,
which are evaluated once per second. In order for a task to be started, all of the start
conditions must be met, while only a single termination condition must be met to trigger
the task termination.

The current set of potential scheduling conditions are:

  * Battery charge percentage
  * Application runtime

    * Run on N second multiples (:c:enumerator:`TASK_PERIODICITY_FIXED`)
    * Run at most every N seconds (:c:enumerator:`TASK_PERIODICITY_LOCKOUT`)

  * Task runtime timeout
  * Application states

Combining these basic options together allows the construction of complex
scheduling conditions in a compact form, for example:

    Run this task once a minute while moving, as long as the battery is over 20% charged
    and the current global time is known. If the battery drops below 15%, or the task has
    been running for over 15 seconds, terminate it.

.. code-block:: c

   struct schedules schedule_list[] = {
     {
      .task_id = SOME_TASK_ID,
      .validity = TASK_VALID_ALWAYS,
      .periodicity_type = TASK_PERIODICITY_FIXED,
      .timeout_s = 15,
      .battery_start_threshold = 20,
      .battery_terminate_threshold = 15,
      .periodicity.fixed.period_s = 60,
      .states_start = TASK_STATES_DEFINE(TR_NOT | INFUSE_STATE_DEVICE_STATIONARY, INFUSE_STATE_TIME_KNOWN),
     },
   };

Task Arguments
**************

Each task schedule can also be assigned arguments related to the task itself.
This allows the behaviour of the task to be customised as the application
desires, without needing to modify the tasks source code. These arguments can
also be updated without needing to perform a full firmware update, in case parameters
need to be tweaked after deployment.

.. code-block:: c

   struct schedules schedule_list[] = {
     {
      .task_id = TASK_ID_IMU,
      .validity = TASK_VALID_ALWAYS,
      .task_args.infuse.imu =
        {
          .accelerometer =
            {
              .range_g = 4,
              .rate_hz = 50,
            },
          .gyroscope =
            {
              .range_dps = 500,
              .rate_hz = 50,
            },
          .fifo_sample_buffer = 100,
        },
     },
   };

Arguments for custom task implementations can be inserted into
:c:struct:`task_schedule` with the
:kconfig:option:`CONFIG_TASK_RUNNER_CUSTOM_TASK_DEFINITIONS` option.

Task Schedule vs Task Implementation
************************************

A task schedule is a description of when a task implementation should be run.
A task schedule is linked to the implementation through the ``.task_id`` field
of a :c:struct:`task_schedule`. A single application can have multiple schedules
referring to the same task implementation, although only a single schedule per
task implementation can be running at a given time.

Schedule Evaluation
*******************

All schedules in an application are evaluated at the same time by the
:c:func:`task_runner_iterate` function, which is required to be run once
a second. This task can be offloaded from the application by calling
:c:func:`task_runner_start_auto_iterate`, which will automatically call
the former function from the :ref:`infuse_workqueue` context.

Task Implementations
********************

Tasks can be implemented as running as either a dedicated thread or as
a delayable workqueue item running on the :ref:`infuse_workqueue`. The
former allows for more flexibility in terms of blocking operations, while
the latter is more lightweight in terms of RAM resources since there is
no need for a dedicated thread stack per task.

Built-in Tasks
==============

Infuse-IoT comes with a selection of builtin task implementations for a range
of common application tasks. Each task uses the standard Zephyr or Infuse-IoT
API, allowing each task to be re-used across any hardware driver that implements
the API.

  * Battery state sampling
  * Environmental sensor sampling
  * GNSS location retrieval
  * IMU controller (3 or 6 axis)
  * :ref:`tdf_api` logger

API Reference
*************

.. doxygengroup:: task_runner_runner_apis
.. doxygengroup:: task_runner_schedule_apis
