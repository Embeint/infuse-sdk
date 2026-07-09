.. _infuse-cloud-rest-api:

REST API
########

The purpose of the Infuse-IoT REST API is to perform device management tasks,
including but not limited to:

  * Device `provisioning`_
  * Device `state telemetry`_
  * Device `command`_ handling
  * Encryption key derivation

These actions are triggered on an as-needed basis by customer automated scripts,
CLI tools, or cloud backends.

The source of truth for the Infuse-IoT REST API is the `OpenAPI`_ schema,
which is available on our hosted `API docs`_, together with a client for
testing requests.

Authentication
**************

Authentication to the Infuse-IoT cloud REST API is done through a JWT Bearer
Token.

.. _provisioning: https://docs.infuse-cloud.io/docs/infuse-iot-cloud/provisioning
.. _state telemetry: https://docs.infuse-cloud.io/docs/infuse-iot-cloud/telemetry
.. _command: https://docs.infuse-cloud.io/docs/infuse-iot-cloud/commands
.. _OpenAPI: https://www.openapis.org
.. _API docs: https://api.infuse-iot.com/docs
