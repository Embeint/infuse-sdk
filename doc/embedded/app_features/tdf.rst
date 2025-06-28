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
   * - :c:enumerator:`TDF_ARRAY_DIFF`
     - Array of TDF readings evenly spaced in time and closely spaced in values

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

Diff Array
----------

The :c:enumerator:`TDF_ARRAY_DIFF` is an extension of :c:enumerator:`TDF_ARRAY_TIME`, which relies
on struct fields being close in value to each other and implicit knowledge about the structure layout
to achieve additional compression. Consider the following arbitarary TDF definition:

.. code-block:: c

   struct tdf_example {
      /** Ambient temperature (millidegrees) */
      int32_t temperature;
      /** Atmospheric pressure (pascals) */
      uint32_t pressure;
   } __packed;

When samples are taken at short intervals, the values of each field are unlikely to change by a large
amount. Instead of saving each reading as a complete 8 byte struct, it could instead be stored as
one base reading, and then a repeating array of differences on each field. For example:

.. code-block:: c

   struct tdf_example_diff_array {
      /** Base reading */
      struct tdf_example base;
      /** Difference from the previous reading */
      struct {
         int8_t diff_temperature;
         int8_t diff_pressure;
      } diffs[];
   } __packed;

As long as the differences on each field fall within ``int8_t`` from the previous value, this can lead to
large packing efficiencies (75% in this example). The original values can be reconstructed as follows:

.. code-block:: c

   struct tdf_example reading[0] = array.base;
   struct tdf_example reading[N+1] = {
      .temperature = reading[N].temperature + array.diffs[N].diff_temperature,
      .pressure = reading[N].pressure + array.diffs[N].diff_pressure,
   };

To limit the complexity of encoding and reconstruction, there are 3 supported variants of diff
encoding.

.. list-table::

   * - Enum
     - Input Type
     - Diff Type
   * - :c:enumerator:`TDF_DATA_FORMAT_DIFF_ARRAY_16_8`
     - ``uint16_t`` / ``int16_t``
     - ``int8_t``
   * - :c:enumerator:`TDF_DATA_FORMAT_DIFF_ARRAY_32_8`
     - ``uint32_t`` / ``int32_t``
     - ``int8_t``
   * - :c:enumerator:`TDF_DATA_FORMAT_DIFF_ARRAY_32_16`
     - ``uint32_t`` / ``int32_t``
     - ``int16_t``

The input data type defines how the encoder views the TDF struct, for example with
:c:enumerator:`TDF_DATA_FORMAT_DIFF_ARRAY_32_8` the encoder will interpret the input as ``uint32_t`` chunks.
The diff type defines the maximum value difference between input chunks that can be
encoded as a valid diff.

Generally, the input data type will be self-evident from the TDF type being encoded. ``struct tdf_example``
from above for example should use either :c:enumerator:`TDF_DATA_FORMAT_DIFF_ARRAY_32_8` or
:c:enumerator:`TDF_DATA_FORMAT_DIFF_ARRAY_32_16`. The choice comes down to the expected differences between
subsequent values. A larger diff type can handle larger differences without falling back to
:c:enumerator:`TDF_ARRAY_TIME`, but consumes more size in the output buffer.

When a reading is of this type, an additional 3 byte header is present after the timestamp structure.

.. code-block:: c

   struct tdf_diff_array_header {
      uint8_t mode_num;
      uint16_t period;
   } __packed;

In contrast to the :c:enumerator:`TDF_ARRAY_TIME` header, the ``mode_num`` field encodes both the number
of diffs present and the mode of the diff encoding.

.. note::

   Encoding data in the :c:enumerator:`TDF_ARRAY_DIFF` format requires :kconfig:option:`CONFIG_TDF_DIFF`.

Index Array
-----------

For high-frequency data sets, the accuracy of timestamping individual samples (logged individually or in a
:c:enumerator:`TDF_ARRAY_TIME`) starts to degrade as the sample period approaches the timestamp resolution
(~15 us, ``1 / 65536``). An example of this is raw audio samples, which might be sampled at 48 kHz
(sample period 20 us). Using the per-sample timestamp options previously described would result in decoded
samples not having a consistent period, despite the input data being sampled at a consistent frequency. This
problem only gets worse as the frequency increases further.

To enable these use-cases, TDF data can be stored as an "Index Array", where instead of attempting to record
timestamps for each individual sample, only the timestamp of the first sample is recorded (accurate to the
15 us resolution), and all future samples are timestamped according to the sample index. This mode does not
attempt to store the actual sampling frequency.

When a reading is of this type, an additional 3 byte header is present after the timestamp structure.

.. code-block:: c

   struct tdf_idx_array_header {
      uint8_t num;
      uint16_t sample_idx;
   } __packed;

The ``sample_idx`` field stores the current (rotating 16 bit) sample index of the recording, with index 0
corresponding to the first sample.

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

Time/Index Array
----------------

When the array type is :c:enumerator:`TDF_ARRAY_TIME` or :c:enumerator:`TDF_ARRAY_IDX`, the field is
the size of a single reading in the array. To obtain the complete payload size it must be multiplied
with the ``num`` field in the time array header.

.. list-table::

   * - Payload Size
     - ``size * time_header.num``
   * - Number TDFs
     - ``time_header.num``

Diff Array
----------

When the array type is :c:enumerator:`TDF_ARRAY_DIFF`, the field is the size of the base reading
in the array. To obtain the complete payload size it must be combined with the information in
the time array header.

.. list-table::

   * - Payload Size
     - ``size + time_header.num * (size / diff_type_size)``
   * - Number TDFs
     - ``1 + time_header.num``

.. note::

   ``diff_type_size`` is the size of an individual diff value. i.e. ``1`` if the diff value is 8 bit
   and ``2`` is the diff value is 16 bit.

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


Index Array
===========

Logging an array of high-frequency TDFs, evenly spaced in time.

.. warning::

   It is important that the timestamp provided to TDF logging functions in array mode is the timestamp
   of the *FIRST* reading, not the *LAST* reading.

.. code-block:: c
   :caption: Low-Level API

   static uint8_t buffer[256];
   struct tdf_buffer_state state;
   struct tdf_acc_4g readings[] = {...};
   uint64_t base_time = epoch_time_now() - ((ARRAY_SIZE(readings) - 1) * reading_period);
   uint8_t remaining = ARRAY_SIZE(readings);
   uint8_t to_log, chunk_size = 16;
   int idx = 0;

   net_buf_simple_init_with_data(&state.buf, buffer, sizeof(buffer));
   tdf_buffer_state_reset(&state);

   while(remaining) {
        to_log = MIN(remaining, chunk_size);
        tdf_add_core(&state, TDF_ACC_4G, sizeof(reading), to_log, base_time, idx,
                     readings + idx, TDF_DATA_FORMAT_IDX_ARRAY);
        /* Only the first sample gets an explicit timestamp */
        base_log = 0;
        idx += to_log;
   }

.. code-block:: c
   :caption: :ref:`tdf_data_logger_api` API

   struct tdf_acc_4g readings[] = {...};
   uint64_t base_time = epoch_time_now() - ((ARRAY_SIZE(readings) - 1) * reading_period);
   int idx = 0;

   for (int i = 0; i < num_buffers; i++) {
      tdf_data_logger_log_core(TDF_DATA_LOGGER_BT_ADV, TDF_ACC_4G, sizeof(readings[0]), ARRAY_SIZE(readings),
                              TDF_DATA_FORMAT_IDX_ARRAY, base_time, idx, readings);
      idx += ARRAY_SIZE(readings);
      base_time = 0;
   }

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

User-defined Types
******************

Infuse-IoT also allows custom user-defined TDFs to be integrated with the framework with
:kconfig:option:`CONFIG_INFUSE_DEFS_GENERATED_DOWNSTREAM`. See :ref:`tooling_user_definitions`
for more details.

API Reference
*************

.. doxygengroup:: tdf_apis
.. doxygengroup:: tdf_util_apis

.. _The Big Night Out (Sommer et al, 2014): https://doi.org/10.1007/978-3-319-03071-5_2
