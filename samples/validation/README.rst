.. _embedded_sample_validation:

Validation
##########

Sample application that performs automated hardware testing of a device.

Production Testing
******************

The validation sample includes a sample server for use in an assembly
environment, based on Google's `OpenHTF`_.

In addition to ensuring all hardware tests from the validation application
pass, the server also handles automated provisioning, multiple SoCs, and
final image flashing.

Sample configurations for the ``nrf54L15DK``, ``nRF9161DK`` and ``Tauro``
board are available under ``samples/validation/examples``.

.. _OpenHTF: https://github.com/google/openhtf
