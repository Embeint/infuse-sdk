.. raw:: html

   <a href="https://www.embeint.com">
     <p align="center">
       <picture>
         <source media="(prefers-color-scheme: dark)" srcset="doc/_static/images/infuse-dark.svg">
         <source media="(prefers-color-scheme: light)" srcset="doc/_static/images/infuse-light.svg">
         <img src="doc/_static/images/infuse-light.svg">
       </picture>
     </p>
   </a>

   <a href="https://github.com/embeint/infuse-sdk/actions/workflows/twister.yml?query=branch%3Amain">
     <img src="https://github.com/embeint/infuse-sdk/actions/workflows/twister.yml/badge.svg?event=schedule">
   </a>
   <a href="https://github.com/embeint/infuse-sdk/actions/workflows/codecov.yml?query=branch%3Amain">
     <img src="https://github.com/embeint/infuse-sdk/actions/workflows/codecov.yml/badge.svg?event=schedule">
   </a>
   <a href="https://codecov.io/gh/Embeint/infuse-sdk">
     <img src="https://codecov.io/gh/Embeint/infuse-sdk/branch/main/graph/badge.svg?token=YR697LEP1F"/>
   </a>

Infuse-IoT is a platform designed to make is simple to create ultra
low-power Internet-of-Things (IoT) solutions. It is a collection of embedded
software, cloud APIs, desktop tools and mobile libraries that enables rapid
development, iteration and management.

Application Features
********************

  * Secure communication interface abstractions
  * Flexible high-level task scheduling
  * Size optimised time-series data logging
  * Cloud mirrored device configuration
  * Remote procedure call server + client
  * Binary diff application image upgrades
  * Extended `Trusted Firmware-M`_ support
  * Post-deployment algorithm loading/updates (Coming soon)

Architecture
************

Embedded Software
=================

The Infuse-IoT embedded stack is built on top of the `Zephyr Project`_, a
next-generation real-time operating system managed by the `Linux Foundation`_.

Cloud Services
==============

Device provisioning and management runs through a rich `REST API`_, while real-time
device data is provided through dedicated `MQTT`_ queues.

Rich Tooling
============

The Infuse-IoT `Python Tools`_ provide CLI interaction with the Cloud REST API,
observation of local devices via Bluetooth and a flexible set of libraries to
write custom scripts for local and cloud device interaction.

Mobile Components
=================

Coming soon!

Supported System-On-Chips
*************************

The Infuse-IoT SDK currently supports the following SoC series:

  * Nordic Semiconductor `nRF52`_
  * Nordic Semiconductor `nRF53`_
  * Nordic Semiconductor `nRF91`_
  * Nordic Semiconductor `nRF54L`_
  * ST Microelectronics `STM32L4x`_
  * ST Microelectronics `STM32WBx5`_ (Coming Soon)

For a complete list of supported boards, see `builtin supported platforms`_.

.. _Nordic Semiconductor: https://www.nordicsemi.com/
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
.. _builtin supported platforms: https://docs.dev.infuse-iot.com/latest/snippets/infuse/README.html
.. _Trusted Firmware-M: https://www.trustedfirmware.org/projects/tf-m/
