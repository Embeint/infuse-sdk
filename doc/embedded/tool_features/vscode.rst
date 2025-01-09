.. _embedded_vscode_integration:

VSCode Integration
##################

Infuse-IoT implements extension west commands to automatically generate VSCode
configuration files for development and debugging.

Editor Configuration
********************

Generating a basic configuration file for Infuse-IoT applications is done with
the following command:

.. code-block:: bash

    west vscode

.. warning::

    This command overwrites any previous content in the listed files.

This generates the following files:

``settings.json``
=================

  * Automatic code formatting:

    *  c: ``clang-format``
    *  python: ``ruff``

  * Indentation settings for ``cmake``, ``c``, ``dts`` and ``kconfig`` files

``extensions.json``
===================

Recommended extensions for working with Zephyr, CMake and C projects.

``snippets.json``
=================

Quick generation of new file prefixes for ``.c`` and ``.h`` headers through typing
``new_file_c`` or ``new_file_h`` in a file.

Application Configuration
*************************

Once an application has been built, Infuse-IoT can generate application specific
configuration files for Intellisense and debugging:

.. code-block:: bash

    west build -b nrf52840dk/nrf52840 zephyr/samples/hello_world
    west vscode -d build/nrf52840dk/nrf52840/hello_world

.. note::

    The previous command assumes you have configured application specific build directories
    with ``west config build.dir-fmt "build/{board}/{app}"``.

``c_cpp_properties.json``
=========================

Intellisense configuration for the specified application.

``launch.json``
===============

Debugger configuration to launch a new instance of, or attach to a running instance of the
application.
