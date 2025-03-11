..
    Infuse-IoT SDK documentation main file

.. _infuse-iot-home:

Infuse-IoT SDK Documentation
############################

.. only:: release

   **Welcome to the Infuse-IoT SDK's documentation for version** |version|.

.. only:: (development or daily)

   **Welcome to the Infuse-IoT SDK's development documentation** (version |version|)

Platform
********

Infuse-IoT is a platform designed to make is simple to create ultra
low-power Internet-of-Things (IoT) solutions. It is a collection of embedded
software, cloud APIs, desktop tools and mobile libraries that enables rapid
development, iteration and management.

.. raw:: html

   <ul class="grid">
      <li class="grid-item">
         <a href="embedded/index.html">
            <span class="grid-icon fa fa-microchip"></span>
            <h2>Embedded Software</h2>
         </a>
         <p>Infuse-IoT Embedded documentation</p>
      </li>
      <li class="grid-item">
         <a href="cloud/index.html">
            <span class="grid-icon fa fa-cloud"></span>
            <h2>Cloud Services</h2>
         </a>
         <p>Infuse-IoT Cloud documentation</p>
      </li>
      <li class="grid-item">
         <a href="tooling/index.html">
            <span class="grid-icon fa fa-wrench"></span>
            <h2>Tooling</h2>
         </a>
         <p>Infuse-IoT Tooling documentation</p>
      </li>
   </ul>

.. toctree::
   :maxdepth: 1
   :hidden:
   :caption: Platform

   platform/design_goals.rst
   platform/security_model.rst
   platform/provisioning.rst
   platform/integrations.rst

Embedded Software
*****************

The Infuse-IoT embedded stack is built on top of the `Zephyr Project`_, a
next-generation real-time operating system managed by the `Linux Foundation`_.

  * Secure communication interface abstractions
  * Secure boot and Over-the-Air upgrades (with diff support)
  * Flexible high-level task scheduling
  * Size optimised time-series data logging
  * Cloud mirrored device configuration
  * Remote procedure call server + client
  * Extended `Trusted Firmware-M`_ support
  * Runtime reconfiguration
  * Dynamic algorithm support (Coming soon)

.. toctree::
   :maxdepth: 1
   :hidden:
   :caption: Embedded

   embedded/index.rst
   embedded/app_features.rst
   embedded/hardware.rst

Cloud Services
**************

Device provisioning and management runs through a rich `REST API`_, while real-time
device data is provided through dedicated `MQTT`_ queues.

.. toctree::
   :maxdepth: 1
   :hidden:
   :caption: Cloud

   cloud/index.rst
   cloud/rest.rst
   cloud/mqtt.rst
   cloud/coap.rst

Rich Tooling
************

The Infuse-IoT `Python Tools`_ provide CLI interaction with the Cloud REST API,
observation of local devices via Bluetooth and a flexible set of libraries to
write custom scripts for local and cloud device interaction.

.. toctree::
   :maxdepth: 1
   :hidden:
   :caption: Tooling

   tooling/index.rst
   tooling/release_framework.rst
   tooling/python_tools.rst
   tooling/user_definitions.rst
   tooling/vscode.rst

Getting Started Guides and Tutorials
************************************

Getting started guides to get setup with the Infuse-IoT platform and tutorials using Infuse-IoT.

.. toctree::
   :maxdepth: 1
   :hidden:
   :caption: Guides and Tutorials

   getting_started/index.rst
   getting_started/embedded/index.rst

Mobile Components
*****************

Coming soon!

Supported System-On-Chips
*************************

The Infuse-IoT SDK currently supports the following SoC series:

  * Nordic Semiconductor `nRF52`_
  * Nordic Semiconductor `nRF53`_
  * Nordic Semiconductor `nRF91`_
  * Nordic Semiconductor `nRF54L`_ (Coming Soon)
  * ST Microelectronics `STM32L4x`_
  * ST Microelectronics `STM32WBx5`_ (Coming Soon)

For a complete list of supported boards, see :ref:`snippet-infuse`.

.. _MQTT: https://mqtt.org
.. _Zephyr Project: https://zephyrproject.org
.. _Linux Foundation: https://www.linuxfoundation.org
.. _REST API: https://api.infuse-iot.com/docs
.. _Python Tools: https://github.com/Embeint/python-tools
.. _nRF52: https://docs.nordicsemi.com/category/nrf-52-series
.. _nRF53: https://docs.nordicsemi.com/category/nrf-53-series
.. _nRF54L: https://docs.nordicsemi.com/category/nrf-54L-series
.. _nRF91: https://docs.nordicsemi.com/category/nrf-91-series
.. _STM32L4x: https://www.st.com/en/microcontrollers-microprocessors/stm32l4-series.html
.. _STM32WBx5: https://www.st.com/en/microcontrollers-microprocessors/stm32wbx5.html
.. _Trusted Firmware-M: https://www.trustedfirmware.org/projects/tf-m/
