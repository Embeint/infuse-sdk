.. _snippet-infuse-bt-ctlr:

Infuse-IoT Bluetooth controller snippet
#######################################

.. code-block:: console

   west build -S infuse-bt-ctlr [...]

Overview
********

The ``infuse-bt-ctlr`` snippet makes upstream compatible Bluetooth controllers
usable with Infuse-IoT by configuring the following parameters:

  * ePacket communication interfaces
  * DFU memory partitions

Supported Infuse Boards
***********************

.. rst-class:: rst-columns

 * :ref:`board_auroch` (nRF9151 + nRF54L15)
 * :ref:`board_tauro` (nRF9151 + nRF52840)
 * :ref:`board_tauro_2` (nRF9151 + nRF54L15)
