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
    * Run N seconds after another schedule finishes (:c:enumerator:`TASK_PERIODICITY_AFTER`)

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
      .battery_start.lower = 20,
      .battery_terminate.lower = 15,
      .periodicity.fixed.period_s = 60,
      .states_start = TASK_STATES_DEFINE(TR_NOT | INFUSE_STATE_DEVICE_STATIONARY, INFUSE_STATE_TIME_KNOWN),
     },
   };

Common Scheduling Fields
************************

Each :c:struct:`task_schedule` contains the following common fields, independent
of the task-specific arguments described in the next section:

``task_id``
  Identifies the task implementation that this schedule starts. Multiple
  schedules can reference the same task ID, although only one schedule for a
  task implementation can run at a time.

``validity``
  Controls when the schedule itself is valid. :c:enumerator:`TASK_VALID_ALWAYS`
  is always eligible, :c:enumerator:`TASK_VALID_ACTIVE` is eligible only while
  :c:enumerator:`INFUSE_STATE_APPLICATION_ACTIVE` is set, and
  :c:enumerator:`TASK_VALID_INACTIVE` is eligible only while it is clear.
  :c:enumerator:`TASK_VALID_PERMANENTLY_RUNS` bypasses normal entry and exit
  checks and restarts the task if it terminates. The
  :c:enumerator:`TASK_LOCKED` flag can be ORed into this field to prevent KV
  store updates from replacing the schedule.

``periodicity_type``
  Selects which member of the ``periodicity`` union is used for start timing.
  A zero value means there is no periodicity condition, so start timing is
  controlled only by the other start conditions.

``boot_lockout_minutes``
  Prevents the task from starting until the application has been running for
  this many minutes. A value of ``0`` disables the boot lockout.

``timeout_s``
  Requests task termination once the current run has lasted this many seconds.
  A value of ``0`` disables timeout-based termination.

``battery_start``
  Optional battery charge thresholds for starting the task. ``lower`` requires
  the battery percentage to be greater than or equal to the configured value,
  while ``upper`` requires it to be less than or equal to the configured value.
  A threshold value of ``0`` disables that side of the range.

  Example:

  .. code-block:: c

     .battery_start.lower = 30,
     .battery_start.upper = 80,

  This schedule can only start when the battery charge is between 30% and 80%,
  inclusive. If only ``lower`` was set, the task could start at 30% or above; if
  only ``upper`` was set, it could start at 80% or below.

``battery_terminate``
  Optional battery charge thresholds for terminating the task. ``lower``
  requests termination when the battery percentage is less than or equal to the
  configured value, while ``upper`` requests termination when it is greater than
  or equal to the configured value. A threshold value of ``0`` disables that
  threshold.

  Example:

  .. code-block:: c

     .battery_terminate.lower = 20,

  Once the task is running, this requests termination if the battery charge
  falls to 20% or below.

``periodicity.fixed.period_s``
  Used with :c:enumerator:`TASK_PERIODICITY_FIXED`. The task can start only
  when the current global time is on an ``N`` second boundary.

``periodicity.lockout.lockout_s``
  Used with :c:enumerator:`TASK_PERIODICITY_LOCKOUT`. The task can start only
  after this many seconds have elapsed since the schedule last started. OR in
  :c:macro:`TASK_RUNNER_LOCKOUT_IGNORE_FIRST` to allow the first run to start
  without waiting for the initial lockout period.

  Example:

  .. code-block:: c

     .periodicity_type = TASK_PERIODICITY_LOCKOUT,
     .periodicity.lockout.lockout_s =
        TASK_RUNNER_LOCKOUT_IGNORE_FIRST | (30 * SEC_PER_MIN),

  The first run may start as soon as the other start conditions pass. After
  that, each run is separated from the previous start time by at least 30
  minutes.

``periodicity.after.schedule_idx`` and ``periodicity.after.duration_s``
  Used with :c:enumerator:`TASK_PERIODICITY_AFTER`. The task can start
  ``duration_s`` seconds after the schedule at ``schedule_idx`` terminates.

  Example:

  .. code-block:: c

     .periodicity_type = TASK_PERIODICITY_AFTER,
     .periodicity.after.schedule_idx = 0,
     .periodicity.after.duration_s = 10,

  This schedule can start 10 seconds after schedule index 0 terminates, assuming
  the other start conditions are also satisfied.

``periodicity.lockout_dynamic_battery``
  Used with :c:enumerator:`TASK_PERIODICITY_LOCKOUT_DYNAMIC_BATTERY`. The
  lockout behaves like :c:enumerator:`TASK_PERIODICITY_LOCKOUT`, but the
  interval is derived from the current battery percentage. The lockout is
  ``lockout_min`` at or below ``battery_min``, ``lockout_max`` at or above
  ``battery_max``, and linearly interpolated between those points.

  Example:

  .. code-block:: c

     .periodicity_type = TASK_PERIODICITY_LOCKOUT_DYNAMIC_BATTERY,
     .periodicity.lockout_dynamic_battery =
        {
           .battery_min = 20,
           .battery_max = 80,
           .lockout_min = 60 * SEC_PER_MIN,
           .lockout_max = 10 * SEC_PER_MIN,
        },

  At 20% battery or below, runs are separated by 60 minutes. At 80% battery or
  above, runs are separated by 10 minutes. Between those thresholds, the lockout
  is linearly interpolated, so a mid-range battery gives a mid-range lockout.

``states_start_timeout_2x_s``
  Optional fallback for the start state conditions. When non-zero,
  ``states_start`` is treated as satisfied once twice this value in seconds has
  elapsed since the schedule last started. Use
  :c:macro:`TASK_STATES_START_TIMEOUT` when initialising this field.

  Example:

  .. code-block:: c

     .states_start_timeout_2x_s = TASK_STATES_START_TIMEOUT(20 * SEC_PER_MIN),
     .states_start = TASK_STATES_DEFINE(INFUSE_STATE_TIME_KNOWN),

  The task can start when time is known. If that state is not set, the state
  condition is still treated as satisfied once 20 minutes have elapsed since the
  schedule last started.

``states_start``
  Application state conditions that must evaluate true before the task can
  start. Construct this field with :c:macro:`TASK_STATES_DEFINE`; conditions are
  ANDed by default, can be inverted with :c:macro:`TR_NOT`, and can be ORed with
  :c:macro:`TR_OR`.

  Example:

  .. code-block:: c

     .states_start = TASK_STATES_DEFINE(
        TR_NOT | INFUSE_STATE_DEVICE_STATIONARY,
        INFUSE_STATE_TIME_KNOWN),

  The task can start only when the device is not stationary and the global time
  is known.

  .. code-block:: c

     .states_start = TASK_STATES_DEFINE(
        INFUSE_STATE_DEVICE_STARTED_MOVING,
        TR_OR | INFUSE_STATE_HIGH_PRIORITY_UPLINK),

  The task can start when either the device has started moving or a high
  priority uplink is requested.

``states_terminate``
  Application state conditions that request task termination when they evaluate
  true. This field uses the same :c:macro:`TASK_STATES_DEFINE`,
  :c:macro:`TR_NOT`, and :c:macro:`TR_OR` helpers as ``states_start``.

  Example:

  .. code-block:: c

     .states_terminate = TASK_STATES_DEFINE(INFUSE_STATE_DEVICE_STATIONARY),

  Once the task is running, this requests termination when the device becomes
  stationary.

``task_logging``
  Common logging configuration for task output. Each entry selects a set of TDF
  loggers and a task-defined TDF mask. The task implementation decides which
  masks are meaningful.

  Example:

  .. code-block:: c

     .task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL,
     .task_logging[0].tdf_mask = TASK_GNSS_LOG_LLHA | TASK_GNSS_LOG_FIX_INFO,

  The task may emit the LLHA and fix information TDFs to the serial logger. The
  ``tdf_mask`` bits are task-specific, so the available values depend on the
  selected ``task_id``.

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
      .task_args.imu =
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

Task argument structures, task IDs, and task logging masks are generated from
``scripts/west_commands/cloud_definitions/tasks.json`` by ``west cloudgen``.
Generated headers are written under
``generated/include/infuse/task_runner/tasks``. The task-specific argument
headers define the ``struct task_<task>_args`` types and constants such as
``TASK_<TASK>_LOG_*``. The common
``generated/include/infuse/task_runner/tasks/infuse_task_args.h`` header
combines them into :c:union:`task_arguments`, which is embedded directly
as :c:member:`task_schedule.task_args`.

Downstream applications can add task definitions by providing an extension
``tasks.json`` to ``west cloudgen``:

.. code-block:: console

   west cloudgen -d path/to/extensions -o path/to/application

Extension task definitions are merged with the built-in definitions.

When a downstream task should be included by the generated
``infuse_tasks.h`` aggregate header, the application should provide a matching
task API header such as ``include/infuse/task_runner/tasks/<task>.h``. That
header typically declares the task implementation entry points or helper macros.

Updating Task Schedules
***********************

Task schedules can be updated at runtime without a full firmware update through the
usage of the :ref:`kv_store_api`. When :kconfig:option:`CONFIG_KV_STORE_KEY_TASK_SCHEDULES`
is enabled, the schedules provided to :c:func:`task_runner_init` are treated as the
default schedules distributed with the application.

Any writes to the underlying task schedule KV slots will replace the default
schedule until a new set of default schedules are distributed. A new set of defaults
are signified by incrementing the :kconfig:option:`CONFIG_TASK_RUNNER_DEFAULT_SCHEDULES_ID`
option. This must be used if the default schedules are changing in a way that
could be incompatible with previous definitions. One example of this is if a new
schedule is inserted in the middle of the default schedule list.

When a new schedule is written to :c:enumerator:`KV_KEY_TASK_SCHEDULES` or a default schedule
reset is triggered by a write to :c:enumerator:`KV_KEY_TASK_SCHEDULES_DEFAULT_ID`, all currently
running tasks are terminated and all schedules are reloaded and revalidated.

Disabling schedule updates
==========================

If there is a particular task schedule that must never be updated for correct
operation of a device, that can be controlled by adding the :c:enum:`TASK_LOCKED`
flag to the :c:member:`task_schedule.validity` field of the schedule like below:

.. code-block:: c

   struct schedules schedule_list[] = {
     {
      .task_id = TASK_ID_IMU,
      .validity = TASK_LOCKED | TASK_VALID_ALWAYS,
     },
   };

This flag will prevent :c:func:`task_runner_schedules_load` from modifying the
provided schedule, regardless of the value saved in the KV store.

Inspecting Encoded Schedules
****************************

Encoded task schedules can be inspected with ``infuse schedule decode``. Pass
the schedule payload as hex or base64 to print a readable description:

.. code-block:: console

   infuse schedule decode <schedule>

Use ``--python`` to emit Python assignment lines instead. This is useful when
turning an existing encoded schedule into a starting point for small edits:

.. code-block:: console

   infuse schedule decode --python <schedule>

The ``python-tools/scripts/encode_task_schedule_example.py`` script shows the
opposite flow: build an ``infuse_iot.task_runner.schedule.TaskSchedule`` in
Python, set common fields, task logging, and task-specific arguments, then print
the encoded bytes as hex or base64. Copy the decoded assignments into a similar
script, adjust the fields of interest, and re-encode the schedule for a KV
update or other deployment path.

Task Schedule vs Task Implementation
************************************

A task schedule is a description of when a task implementation should be run.
A task schedule is linked to the implementation through the :c:member:`task_schedule.task_id`
field. A single application can have multiple schedules referring to the same
task implementation, although only a single schedule per task implementation can
be running at a given time.

Schedule Evaluation
*******************

All schedules in an application are evaluated at the same time by the
:c:func:`task_runner_iterate` function, which is required to be run once
a second. This task can be offloaded from the application by calling
:c:func:`task_runner_start_auto_iterate`, which will automatically call
the former function from the :ref:`infuse_workqueue` context.

The application is able to receive notifications of when a schedule is started,
requested to terminate, or stopped, by assigning a :c:type:`task_schedule_event_cb_t`
to the appropriate :c:member:`task_schedule_state.event_cb` field **AFTER** the
task runner is initialised with :c:func:`task_runner_init`.

Schedule Event notifications
****************************

If required, applications can register to be notified of scheduling events for
a given schedule. The available events are defined in :c:enum:`task_schedule_event`.
To register for callbacks on these events, populate :c:member:`task_schedule_state.event_cb`
on the same index as the schedule of interest, after the call to :c:func:`task_runner_init`.
For example to subscribe to scheduling callbacks for the battery task:

.. code-block:: c

   struct schedules schedules[] = {
     {
      .task_id = TASK_ID_IMU,
      .validity = TASK_VALID_ALWAYS,
     },
     {
      .task_id = TASK_ID_BATTERY,
      .validity = TASK_VALID_ALWAYS,
     },
   };
   TASK_SCHEDULE_STATES_DEFINE(states, schedules);

   void my_callback(const struct task_schedule *schedule, enum task_schedule_event event)
   {
      ...
   }

   int main(void)
   {
      task_runner_schedules_load(0, schedules, ARRAY_SIZE(schedules));
      task_runner_init(schedules, states, ARRAY_SIZE(schedules), ...);
      states[1].event_cb = my_callback;
   }

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
  * Wi-Fi Access Point & LTE Cell scanning
  * Nearby Bluetooth device scanner
  * :ref:`tdf_api` logger

API Reference
*************

.. doxygengroup:: task_runner_runner_apis
.. doxygengroup:: task_runner_schedule_apis
