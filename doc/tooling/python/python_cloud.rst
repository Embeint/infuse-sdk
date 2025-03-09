.. _python_cloud:

Infuse-IoT Cloud
################

The cloud tool can be use to display information about boards and organisations
your API key has access to. Information from these commands is required when
provisioning new hardware (:ref:`python_provision`).

Organisations
*************

The UUIDs of Infuse-IoT organisations can be displayed with the following command:

.. code:: bash

    infuse cloud orgs list

.. code-block:: bash
    :caption: Example organisation list

    Name               ID
    -----------------  ------------------------------------
    test organisation  413c1966-9186-40da-b412-590afb10c301
    Embeint            cb6c332d-649d-4757-b098-c54421243383

Boards
******

The UUIDs and metadata of hardware platforms can be displayed with the following command:

.. code:: bash

    infuse cloud boards list

.. code-block:: bash
    :caption: Example board list

    Name               ID                                    SoC       Organisation       Description
    -----------------  ------------------------------------  --------  -----------------  ---------------------------------------------------
    nRF7002DK          7bd7af73-875c-4daf-bb34-04cf6b89edcc  nRF5340   test organisation  Nordic Semiconductor nRF7002 Development Kit
    nRF9151DK          a5c3c65f-c0fc-41e5-9a7c-5691059c66ec  nRF9151   test organisation  Nordic Semiconductor nRF9151 Development Kit
    nRF9160DK          07d77eed-cfb0-4794-9e72-f1048ccd36e5  nRF9160   test organisation  Nordic Semiconductor nRF9160 Development Kit
    nRF9161DK          b0bc6396-62b7-45d9-85b3-04b62e58946e  nRF9161   test organisation  Nordic Semiconductor nRF9161 Development Kit
