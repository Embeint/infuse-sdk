.. _platform-provisioning:

Device Provisioning
###################

In order to be used with Infuse-IoT, each individual device must be provisioned
on the cloud. The provisioning process associates several pieces of information
together:

.. list-table::
   :header-rows: 1

   * - Name
     - Source
     - Description
   * - Infuse Device ID
     - Random/User Provided
     - Device identifier within the Infuse-IoT platform
   * - Organisation
     - User
     - Organisation within Infuse-IoT the device belongs to
   * - Board
     - Hardware
     - Name of the board (e.g ``nrf52840dk``)
   * - Hardware Identifier
     - Hardware
     - Unique identifier within a SoC

Provisioning a device enables the Infuse-IoT cloud services to route received
data to the correct customer MQTT streams and enables the tracking and updates
of device metadata.

Hardware provisioning is performed using the ``infuse provision`` command, see :ref:`python_provision`.

Associated Metadata
===================

Boards also support optional and required metadata fields for hardware specific information.
If a metadata field is required, it must be provided at initial provisioning time.
