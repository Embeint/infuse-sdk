..
    Infuse-IoT SDK documentation main file

.. _infuse-iot-home:

Infuse-IoT SDK Documentation
############################

.. only:: release

   **Welcome to the Infuse-IoT SDK's documentation for version** |version|.

.. only:: (development or daily)

   **Welcome to the Infuse-IoT SDK's development documentation** (version |version|)

Infuse is an IoT platform designed for the next generation of ultra low-power
embedded devices. Our goal is to empower developers to create machine-learning
enabled solutions with the lowest possible effort, reaching beyond cloud
connectivity down into the application.

.. raw:: html

   <ul class="grid">
      <li class="grid-item">
         <a href="platform/index.html">
            <span class="grid-icon fa fa-fire"></span>
            <h2>Introduction</h2>
         </a>
         <p>Introducing the Infuse-IoT SDK</p>
      </li>
      <li class="grid-item">
         <a href="cloud/index.html">
            <span class="grid-icon fa fa-cloud"></span>
            <h2>Cloud Services</h2>
         </a>
         <p>Infuse-IoT Cloud documentation</p>
      </li>
      <li class="grid-item">
         <a href="embedded/index.html">
            <span class="grid-icon fa fa-microchip"></span>
            <h2>Embedded Firmware</h2>
         </a>
         <p>Infuse-IoT Embedded documentation</p>
      </li>
      <li class="grid-item">
         <a href="develop/getting_started/index.html">
            <span class="grid-icon fa fa-map-signs"></span>
            <h2>Getting Started Guide</h2>
         </a>
         <p>Follow this guide to get started with the Infuse-IoT SDK</p>
      </li>
      <li class="grid-item">
         <a href="snippets/infuse/README.html">
            <span class="grid-icon fa fa-object-group"></span>
            <h2>Supported Boards</h2>
         </a>
         <p>List of supported boards and platforms.</p>
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

.. toctree::
   :maxdepth: 1
   :hidden:
   :caption: Cloud

   cloud/index.rst
   cloud/rest.rst
   cloud/mqtt.rst
   cloud/coap.rst

.. toctree::
   :maxdepth: 1
   :hidden:
   :caption: Embedded

   embedded/index.rst
   embedded/features.rst
   embedded/hardware.rst
