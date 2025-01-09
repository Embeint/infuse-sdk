.. _data_logger_api:

Data Logger
###########

The data logger API is the common Infuse-IoT abstraction for data storage.
Storage backends can be either persistent (e.g. Flash, SD card) or transient
(e.g. Bluetooth, LTE). The API is block-oriented, with each block having a
defined data type (:c:enum:`infuse_type`) and size. Each block is self-contained
and can therefore be parsed in isolation.

Logger Types
************

Peristent Loggers
=================

Persistent loggers save data to some location that enables data read-back. This
is typically a SPI-NOR flash device or an external SD card (for high datarate
applications). Blocks are typically 512 bytes, with each block containing 2
bytes of overhead for metadata (Wrap index and block type).

Transient Loggers
=================

Transient loggers can handle data using the same API as the persistent loggers,
but once written the data cannot be read back. These are typically wireless
interfaces that implement the data logger API to simplify the implementation of
higher level code.

Higher-Level Interfaces
***********************

The data logger API is a low level interface which only handles complete blocks.
Most application usage of this API will be through higher-level abstractions which
handle constructing blocks in a manner that makes sense for the block type.

  * :ref:`tdf_data_logger_api`: Tagged Data Format logging

.. toctree::
   :maxdepth: 1
   :glob:
   :hidden:

   data_logger/*

API Reference
*************

.. doxygengroup:: data_logger_apis
