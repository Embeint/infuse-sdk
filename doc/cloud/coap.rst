.. _infuse-cloud-coap-server:

CoAP Server
###########

The Infuse-IoT CoAP server hosts the following kinds of files:

  * Application images for OTA device firmware upgrades (DFU)
  * Application diff files for optimised DFU
  * nRF91 modem co-processor diff files
  * Test files for CI validation

Authentication
**************

All access to the CoAP server is protected via a DTLS v1.3 connection,
authenticated using a PSK from the Infuse-IoT
:ref:`platform-security-model`. File access permissions are determined
based on the Infuse-IoT ID associated with the DTLS identity.

There is additionally one hardcoded Identity-PSK pair which has access
solely to the hosted test files for the purposes of CI validation.
