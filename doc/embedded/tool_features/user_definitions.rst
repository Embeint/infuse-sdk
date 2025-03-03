.. _embedded_user_definitions:

Extending Infuse-IoT Definitions
################################

While Infuse-IoT contains many base definitions for the :ref:`tdf_api`, :ref:`rpc_api` and
:ref:`kv_store_api` subsystems, users may wish to extend any or all of these definitions to
support their custom applications.

Generate Definitions
********************

Base definitions are extended by providing the location of additional ``tdf.json``, ``rpc.json``
and ``kv_store.json`` files to the ``west cloudgen`` command. Assuming that the downstream
repository is named ``my_repo``, the recommended folder structure is:

.. code-block:: none

   workspace/
   ├── infuse-sdk/
   ├── zephyr/
   └── my_repo/
       └── extensions/
           ├── rpc.json
           ├── tdf.json
           └── kv_store.json

Once these extensions are defined, the output files are (re)generated with the following command:

.. code-block:: bash

    west cloudgen -d my_repo/extensions -o my_repo

The above command will generate output files into the ``generated`` directory like so:

.. code-block:: none

   workspace/
   ├── infuse-sdk/
   ├── zephyr/
   └── my_repo/
       ├── extensions/
       └── generated/

.. note::

    Each individual ``.json`` file is optional, i.e. you can provide only ``rpc.json``, or any
    other combination of files.

Inject Files Into Build System
******************************

Once generated, the new files need to be injected into the build system, and the original
definitions disabled.

.. code-block:: none

   workspace/
   ├── infuse-sdk/
   ├── zephyr/
   └── my_repo/
       ├── generated/
       |    └── include/
       |    ├── Kconfig.kv_keys
       |    ├── Kconfig.rpc_commands
       |    ├── rpc_command_runner.c
       |    └── include/
       ├── CMakeLists.txt
       └── Kconfig.my_repo

.. code-block:: cmake
   :caption: CMakeLists.txt

   if(CONFIG_INFUSE_DEFS_GENERATED_DOWNSTREAM)
     zephyr_include_directories(generated/include)
     zephyr_sources_ifdef(CONFIG_INFUSE_RPC generated/rpc_command_runner.c)
   endif()

.. code-block:: kconfig
   :caption: Kconfig.my_repo

   # Disable the original Infuse-IoT definitions
   configdefault INFUSE_DEFS_GENERATED_DOWNSTREAM
      default y

   # Include generated KV and RPC definitions
   if INFUSE_DEFS_GENERATED_DOWNSTREAM
   rsource "generated/Kconfig.kv_keys"
   rsource "generated/Kconfig.rpc_commands"
   endif # INFUSE_DEFS_GENERATED_DOWNSTREAM

JSON File Format
****************

For the expected format of the extension ``.json`` files, please refer to the
base definitions in ``infuse-sdk/scripts/west_commands/cloud_definitions/`` and
the sample extensions in ``infuse-sdk/tests/subsys/definitions_extend/``.
