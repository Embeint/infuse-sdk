.. _infuse_workqueue:

Infuse Workqueue
################

The Infuse workqueue is a common `Zephyr Workqueue`_ available for application use.
In contrast to the Zephyr system workqueue, it is acceptable to block the Infuse
workqueue for small durations (<100ms).

.. warning::

    The Infuse workqueue is the context used by :c:func:`task_runner_start_auto_iterate`
    and task runner workqueue tasks, so it is crtical that blocking operations complete
    within 1 second to ensure consistent application operation.

API Reference
*************

.. doxygengroup:: infuse_work_q_apis

.. _Zephyr Workqueue: https://docs.zephyrproject.org/latest/kernel/services/threads/workqueue.html
