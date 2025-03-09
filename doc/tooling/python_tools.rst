.. _tooling_python_tools:

Python Tools
############

Infuse-IoT includes a python package, ``infuse_iot``, available on `Github`_.

Installation
************

In order to support :ref:`tooling_user_definitions`, we recommend installing the package from
source with the `editable`_ flag (``--editable``, ``-e``).

.. code:: bash

    git clone git@github.com:Embeint/python-tools.git
    pip install --editable python-tools

Tools
*****

The package exposes its functionality through the ``infuse`` CLI program.
A complete list of supported subcommands can always be generated through ``infuse --help``.
The package contains the following functionality:

  * :ref:`python_gateways`: Bluetooth & serial bridge applications
  * :ref:`python_credentials`: Secret management
  * :ref:`python_cloud`: Display cloud metadata (Organisations, boards, etc)
  * :ref:`python_provision`: Provision hardware with Infuse-IoT cloud
  * :ref:`python_tdf`: Tagged Data Format viewers
  * :ref:`python_rpc`: Local remote procedure calls (Serial & Bluetooth)
  * :ref:`python_ota_upgrade`: Update local devices to a new software release
  * :ref:`python_bt_log`: Viewing of encrypted Bluetooth debug logs

.. note::

    These tools are for local development, management and scripting applications.
    Unless explicitly specified otherwise, they do not forward data to Infuse-IoT
    cloud services. As a result, actions such as setting KV store values will not
    automatically propagate to the cloud services and will need to be determined
    by the cloud autonomously.

.. toctree::
   :maxdepth: 1
   :hidden:
   :caption: Tools

   python/python_gateways.rst
   python/python_credentials.rst
   python/python_cloud.rst
   python/python_provision.rst
   python/python_tdf.rst
   python/python_rpc.rst
   python/python_ota_upgrade.rst
   python/python_bt_log.rst

.. _Github: https://github.com/Embeint/python-tools
.. _editable: https://pip.pypa.io/en/stable/topics/local-project-installs/#editable-installs
