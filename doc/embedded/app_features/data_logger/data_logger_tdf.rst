.. _tdf_data_logger_api:

TDF Data Logger
###############

A wrapper over the :ref:`data_logger_api` API for buffering :ref:`tdf_api`
readings in RAM until a complete block is constructed. Once constructured, the
block is automatically written to the underlying data logger.

TDF data loggers are instantiated as a :dtcompatible:`embeint,tdf-data-logger` node
as a child node of a :dtcompatible:`embeint,data-logger` node. For example:

.. code-block:: devicetree

	data_logger_flash: data_logger_flash {
		compatible = "embeint,data-logger-flash-map", "embeint,data-logger";
		partition = <&data_logger_partition>;

		tdf_logger_flash: tdf_logger_flash {
			compatible = "embeint,tdf-data-logger";
		};
	};

Remote TDF Data Logger
**********************

The TDF data logger abstraction also supports logging TDFs that originated on a remote device. To mark a
:dtcompatible:`embeint,tdf-data-logger` as logging remote TDFs, add the ``tdf-remote`` property onto the
logger and set the remote ID with :c:func:`tdf_data_logger_remote_id_set`. The remote ID can be updated
arbitrarily at runtime, but buffered data will be flushed to the underlying logger each time it changes.

.. note::

   Multiple TDF data loggers can co-exist on top of a single :dtcompatible:`embeint,data-logger`
   instance. For example:

   .. code-block:: devicetree

      data_logger_exfat: data_logger_exfat {
         compatible = "embeint,data-logger-exfat", "embeint,data-logger";
         disk = < &mmc >;
         tdf_logger_exfat: tdf_logger_removable: tdf_logger_exfat {
               compatible = "embeint,tdf-data-logger";
         };
         tdf_logger_remote: tdf_logger_remote {
               compatible = "embeint,tdf-data-logger";
               tdf-remote;
         };
      };

API Reference
*************

.. doxygengroup:: tdf_data_logger_apis
