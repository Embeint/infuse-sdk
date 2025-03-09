.. _python_rpc:

RPC Runner
##########

The ``rpc`` subcommand can be used to send and receive commands to local devices
via serial and Bluetooth using :ref:`rpc_api`.

In general, the tool uses the following process to run the command:

  1. Requests a connection to the target device from the gateway bridge
  2. Send the request to the target device
  3. Wait for the command response
  4. Disconnect from the target device
  5. Display the results

.. note::

    Running RPCs requires a running instance of a :ref:`python_gateways` bridge.

Supported Commands
******************

The full list of currently supported commands can be displayed with ``infuse rpc --help``:

.. code::

   > infuse rpc --help
   usage: infuse rpc [-h] (--gateway | --id ID) [--conn-log] <command> ...

   Run remote procedure calls on devices

   options:
     -h, --help            show this help message and exit
     --gateway             Run command on local gateway
     --id ID               Infuse ID to run command on
     --conn-log            Request logs from remote device

   commands:
     <command>
       application_info    Query basic application versions and state
       bt_connect_infuse   Connect to an Infuse-IoT Bluetooth device
       bt_disconnect       Disconnect from a Bluetooth device
       coap_download       Download a file from a COAP server (Infuse-IoT DTLS protected)
       data_logger_read    Read data from data logger
       data_logger_state   Get state of a data logger
       fault               Immediately trigger an exception on the device
       file_write_basic    Write a file to the device
       gravity_reference_update
                           Store the current accelerometer vector as the gravity reference
       kv_bt_peer          Configure the peer Bluetooth address
       kv_read             Read values from the KV store
       kv_reflect_crcs     Read KV store reflection crc values
       kv_write            Write an arbitrary kv value
       last_reboot         Retrieve information pertaining to the previous reboot
       lte_at_cmd          Run AT command against LTE modem
       lte_modem_info      Get LTE modem information
       lte_pdp_ctx         Set the LTE PDP context (IP family & APN)
       lte_state           Get current LTE interface state
       reboot              Reboot the device after a delay
       security_state      Query current security state and validate identity
       sym_read            Read arbitrary memory (NO ADDRESS VALIDATION PERFORMED)
       time_get            Get the current time knowledge of the device
       time_set            Set the current time of the device
       wifi_configure      Set the WiFi network SSID and PSK
       wifi_scan           Scan for WiFi networks
       wifi_state          Get current WiFi interface state
       zbus_channel_state  Query current state of zbus channel

Running Commands
****************

Running a RPC on a remote device requires specifying the desired target device. This can
be either ``--gateway`` to run the command on the local serial gateway device (no matter its ID),
or ``--id $INFUSE_ID`` to specify a local Bluetooth device.

The target device is then followed by the desired command name and any command specific arguments.

.. code::

   > infuse rpc --id 0xcc0000eb1e000000 time_get
             Source: GNSS
        Remote Time: 2025-03-10 01:54:52.837
         Local Time: 2025-03-10 01:54:52.150
             Synced: 2193 seconds ago
