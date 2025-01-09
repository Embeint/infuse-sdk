.. _tdf_api:

Tagged Data Format (TDF)
########################

Tagged Data Format is a block-oriented time-series data logging format. The Infuse-IoT
implementation is an extension of the format described in `The Big Night Out (Sommer et al, 2014)`_.

The TDF abstraction is most commonly used via the :ref:`tdf_data_logger_api` API, which
automatically handles the logging of completed blocks.

Reading Format
**************

The basic format of a TDF reading on a block is the following:

.. list-table::
   :header-rows: 1

   * - Header
     - Size
     - Timestamp
     - Array Header
     - Reading Data
   * - 2 bytes
     - 1 byte
     - 0 to 6 bytes
     - 0 or 3 bytes
     - 0 to 255 bytes

Due to the inclusion of a size field in the logged data, TDF readings can be either
a fixed size (e.g. :c:struct:`tdf_acc_4g`), or a variable size (e.g.
:c:struct:`tdf_algorithm_class_histogram`).

TDF Header
==========

The 2 byte header contains the metadata that parsers use to consume
additional bytes from the buffer.

.. list-table::

   * - Array type
     - 2 bits
   * - Timestamp type
     - 2 bits
   * - Reading ID (:c:enum:`tdf_builtin_id`)
     - 12 bits

Timestamp Types
===============

Each TDF reading is associated with a single time point using the :ref:`epoch_time_api` API.
To increase packing efficiency, the last logged timestamp on a block is preserved while parsing,
so that readings spaced closely together in time can use delta values instead of complete timestamps.

.. list-table::
   :header-rows: 1

   * - Type
     - Size
     - Description
   * - :c:enumerator:`TDF_TIMESTAMP_NONE`
     - 0
     - No reading timestamp
   * - :c:enumerator:`TDF_TIMESTAMP_ABSOLUTE`
     - 6
     - Absolute timestamp
   * - :c:enumerator:`TDF_TIMESTAMP_RELATIVE`
     - 2
     - [0 to 1] second increment from block timestamp
   * - :c:enumerator:`TDF_TIMESTAMP_EXTENDED_RELATIVE`
     - 3
     - [-128 to 127] second increment from block timestamp

Array Types
===========

TDF array types enable improved data packing efficiency when
multiple instances of the same reading are being logged with
predictable timestamps.

.. list-table::

   * - :c:enumerator:`TDF_ARRAY_NONE`
     - Single TDF reading
   * - :c:enumerator:`TDF_ARRAY_TIME`
     - Array of TDF readings evenly spaced in time

No Array
--------

No array is a single TDF reading with no additional headers.

Time Array
----------

When a reading is of type :c:enumerator:`TDF_ARRAY_TIME`, an additional 3 byte header is present
after the timestamp structure.

.. code-block:: c

   struct tdf_time_array_header {
      uint8_t num;
      uint16_t period;
   } __packed;

The ``num`` field specifies how many copies of the TDF reading exist in the payload, while ``period``
specifies the time between readings. The timestamp of each reading in the array is calculated using
the following formula: ``timestamp[N] = timestamp_base + (N * period)``.

Size
====

The size field is used to enable parsers to be able to jump forward to the next TDF on a block,
even if the parser is not aware of the data type of the preceding reading. The meaning of this
field changes depending on the array type of the reading.

No Array
--------

When the array type is :c:enumerator:`TDF_ARRAY_NONE`, the field is simply the size of the trailing
reading data.

.. list-table::

   * - Payload Size
     - ``size``
   * - Number TDFs
     - ``1``

Time Array
----------

When the array type is :c:enumerator:`TDF_ARRAY_TIME`, the field is the size of a single reading
in the array. To obtain the complete payload size it must be multiplied with the ``num`` field in
the time array header.

.. list-table::

   * - Payload Size
     - ``size * time_header.num``
   * - Number TDFs
     - ``time_header.num``

Reading Data
============

The remainder of a reading is the trailing data array. The format of the array is simply a binary
packed array. The currently defined ID to data structure mappings can be found at this page:

.. toctree::
   :maxdepth: 1

   tdf/definitions.rst

Logging Examples
****************

When possible, the type safe logging macros should be preferred, as they validate that the
type of the passed data pointer matches the type associated with the TDF ID.

.. note::

   The type safe macros are not possible to use for TDFs with a trailing variable length array,
   since instantiations of the type with a defined length are by definition a different type.

Single TDF
==========

Logging a single TDF at an arbitrary point in time.

.. code-block:: c
   :caption: Low-Level API

   static uint8_t buffer[32];
   struct tdf_buffer_state state;
   struct tdf_acc_4g reading = {
      .sample = {0, -1000, 2000},
   };

   net_buf_simple_init_with_data(&state.buf, buffer, sizeof(buffer));
   tdf_buffer_state_reset(&state);

   /* TDF_ADD is preferred */
   TDF_ADD(&state, TDF_ACC_4G, 1, epoch_time_now(), 0, &reading);
   tdf_add(&state, TDF_ACC_4G, sizeof(reading), 1, epoch_time_now(), 0, &reading);

.. code-block:: c
   :caption: :ref:`tdf_data_logger_api` API

   struct tdf_acc_4g reading = {
      .sample = {0, -1000, 2000},
   };

   /* TDF_DATA_LOGGER_LOG is preferred */
   TDF_DATA_LOGGER_LOG(TDF_DATA_LOGGER_BT_ADV, TDF_ACC_4G, epoch_time_now(), &reading);
   tdf_data_logger_log(TDF_DATA_LOGGER_BT_ADV, TDF_ACC_4G, sizeof(reading), epoch_time_now(), &reading);

Time Array
==========

Logging an array of TDFs, evenly spaced in time.

.. warning::

   It is important that the timestamp provided to TDF logging functions in array mode is the timestamp
   of the *FIRST* reading, not the *LAST* reading.

.. code-block:: c
   :caption: Low-Level API

   static uint8_t buffer[256];
   struct tdf_buffer_state state;
   struct tdf_acc_4g readings[] = {...};
   uint32_t reading_period = INFUSE_EPOCH_TIME_TICKS_PER_SEC / 100;
   uint64_t base_time = epoch_time_now() - ((ARRAY_SIZE(readings) - 1) * reading_period);

   net_buf_simple_init_with_data(&state.buf, buffer, sizeof(buffer));
   tdf_buffer_state_reset(&state);

   /* TDF_ADD is preferred */
   TDF_ADD(&state, TDF_ACC_4G, ARRAY_SIZE(readings), base_time,
           reading_period, readings);
   tdf_add(&state, TDF_ACC_4G, sizeof(readings[0]), ARRAY_SIZE(readings),
           base_time, reading_period, readings);

.. code-block:: c
   :caption: :ref:`tdf_data_logger_api` API

   struct tdf_acc_4g readings[] = {...};
   uint32_t reading_period = INFUSE_EPOCH_TIME_TICKS_PER_SEC / 100;
   uint64_t base_time = epoch_time_now() - ((ARRAY_SIZE(readings) - 1) * reading_period);

   /* TDF_DATA_LOGGER_LOG_ARRAY is preferred */
   TDF_DATA_LOGGER_LOG_ARRAY(TDF_DATA_LOGGER_BT_ADV, TDF_ACC_4G, ARRAY_SIZE(readings),
                             base_time, reading_period, readings);
   tdf_data_logger_log_array(TDF_DATA_LOGGER_BT_ADV, TDF_ACC_4G, sizeof(readings[0]), ARRAY_SIZE(readings),
                             base_time, reading_period, readings);


Embedded Parsing
****************

If required, embedded devices can parse a TDF block through the :c:func:`tdf_parse` API.

.. code-block:: c

   uint8_t tdf_block[] = { /* TDF payload exists in here */ };
   struct tdf_buffer_state state;
   struct tdf_parsed tdf;

   /* Initialise parser */
   tdf_parse_start(&state, tdf_block, sizeof(tdf_block));
   /* Loop while TDFs exist on block */
   while (tdf_parse(&state, &parsed) == 0) {
      /* Handle parsed TDF data */
      LOG_INF("ID: %d Timestamp: %lld Num: %d Data: %p", parsed.tdf_id, parsed.time, parsed.tdf_num, parsed.data);
   }

API Reference
*************

.. doxygengroup:: tdf_apis
.. doxygengroup:: tdf_util_apis

.. _The Big Night Out (Sommer et al, 2014): https://doi.org/10.1007/978-3-319-03071-5_2
