.. _data_logger:

Data Logger
###########

Overview
********

A basic data gathering application that reads from the Battery, IMU and
environmental sensors at fixed frequencies.

Logging Backends
****************

If available, data will be logged solely to an external SD card.
Otherwise, data will be streamed over serial and UDP interfaces, unless UDP
is implemented over LTE in which case it will not be used due to excessive
data volumes.

.. warning::
    Modifying this sample to log to UDP over LTE WILL cost significant amounts of money!!!
