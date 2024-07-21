.. _civil_time_api:

Epoch Time
##########

Until external knowledge is provided, the only timestamp available to an
embedded device is the local device uptime. In order for local events to
be synchronised between multiple devices, the local uptime must be referenced
against an external time standard, known as a time epoch. These time standards
are usually defined against a fixed `UTC`_ instant.

The most well known time epoch is the `Unix time`_ epoch, which is defined as
the number of non-leap seconds that have elapsed since
``00:00:00 1970-01-01 UTC``. Unfortunately unix time is not monotonic or a
continuous time source, which means that times can both decrease in value and
jump backwards or forward in time.

As we require a monotonic and continuous time source, the Infuse-IoT platform
uses the GPS time epoch as the base for all timestamped operations. The GPS
time epoch begins at ``00:00:00 1980-01-06 UTC``.

Implementation Details
**********************

The Infuse-IoT epoch time functionality is built upon the `Zephyr timeutil`_
library.

To obtain sub-second time resolution, the epoch time API encodes the current
time into a ``uint64_t``. The top 48 bits are the current GPS seconds count.
The bottom 16 bits encode the sub-second time portion at a resolution of
``1/65536`` seconds. This maps cleanly onto the standard 32768 Hz low-frequency
oscillators typically used for low-power time synchronisation.

API Reference
*************

.. doxygengroup:: civil_time_apis

.. _Zephyr timeutil: https://docs.zephyrproject.org/latest/kernel/timeutil.html
.. _UTC: https://en.wikipedia.org/wiki/Coordinated_Universal_Time
.. _Unix time: https://en.wikipedia.org/wiki/Unix_time
