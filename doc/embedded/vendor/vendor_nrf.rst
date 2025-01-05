.. _infuse-embedded-vendor-nrf:

Nordic Semiconductor
####################

Provisioning
************

nRF provisioning information is stored in the ``CUSTOMER`` region of "User information
configuration registers" (UICR). These values are erased when a mass erase or recovery
operation is performed (``nrfjprog --recover`` or ``west flash --erase``). Devices
must be re-provisioned with ``infuse provision --nrf`` after erasure.
