.. _infuse-cloud-rest-api:

REST API
########

The purpose of the Infuse-IoT REST API is to perform device management tasks,
including but not limited to:

  * Device provisioning
  * Device state querying & control
  * Command queue & query
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

.. _OpenAPI: https://www.openapis.org
.. _API docs: https://api.infuse-iot.com/docs
