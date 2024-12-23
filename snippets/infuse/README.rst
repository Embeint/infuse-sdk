.. _snippet-infuse:

Infuse-IoT SDK snippet (infuse)
###############################

.. code-block:: console

   west build -S infuse [...]

Overview
********

The ``infuse`` snippet makes upstream compatible boards usable with Infuse-IoT
by configuring the following parameters:

  * ePacket communication interfaces
  * Data logger instances
  * Low-Power operation configuration
  * Reboot state storage
  * Key-Value store configuration

Supported Upstream Boards
*************************

.. rst-class:: rst-columns

 * `Nordic Thingy53`_
 * `Nordic nRF5340 DK`_
 * `Nordic nRF52840 DK`_
 * `Nordic nRF9151 DK`_
 * `Nordic nRF9160 DK`_
 * `Nordic nRF9161 DK`_

Supported Infuse Boards
***********************

.. rst-class:: rst-columns

 * `Nordic nRF7002 DK`_
 * :ref:`board_tauro` (nRF9151 + nRF52840)

Simulation Targets
******************

.. rst-class:: rst-columns

 * `ARM V2M MPS2`_
 * `nRF52 BSIM`_


.. _Nordic nRF52840 DK: https://www.nordicsemi.com/Products/Development-hardware/nRF52840-DK
.. _Nordic nRF5340 DK: https://www.nordicsemi.com/Products/Development-hardware/nRF5340-DK
.. _Nordic Thingy53: https://www.nordicsemi.com/Products/Development-hardware/Nordic-Thingy-53
.. _Nordic nRF7002 DK: https://www.nordicsemi.com/Products/Development-hardware/nRF7002-DK
.. _Nordic nRF9151 DK: https://www.nordicsemi.com/Products/Development-hardware/nRF9151-DK
.. _Nordic nRF9160 DK: https://www.nordicsemi.com/Products/Development-hardware/nRF9160-DK
.. _Nordic nRF9161 DK: https://www.nordicsemi.com/Products/Development-hardware/nRF9161-DK
.. _ARM V2M MPS2: https://docs.zephyrproject.org/latest/boards/arm/mps2/doc/mps2_an385.html
.. _nRF52 BSIM: https://docs.zephyrproject.org/latest/boards/native/nrf_bsim/doc/nrf52_bsim.html
