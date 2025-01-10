.. _rpc_api:

Remote Procedure Calls (RPC)
############################

Infuse-IoT implements a compact RPC protocol for device management.

RPC Server
**********

The embedded RPC server handles :c:enumerator:`INFUSE_RPC_CMD` packets and generates
:c:enumerator:`INFUSE_RPC_RSP` packets in response. The command handling is automatically invoked
by the default ePacket packet handler function :c:func:`epacket_default_receive_handler`.
The server has a dedicated thread context for running commands, with a configurable stack size
(:kconfig:option:`CONFIG_INFUSE_RPC_SERVER_STACK_SIZE`).

RPC Client
**********

The RPC client library enables running commands on remote devices. This is currently
limited to devices that share the same root network key and commands that only require
:c:enumerator:`EPACKET_AUTH_NETWORK` level authentication.

Command Configuration
*********************

Each command is enabled through a dedicated Kconfig symbol, for example
:kconfig:option:`CONFIG_INFUSE_RPC_COMMAND_APPLICATION_INFO`. Each command also has a configurable
authentication level, which determines the encryption level required on the packet for the command
to be executed, for example :kconfig:option:`CONFIG_INFUSE_RPC_COMMAND_APPLICATION_INFO_REQUIRED_AUTH`.

Built-in Commands
*****************

.. doxygenenum:: rpc_builtin_id

User-defined Commands
*********************

Infuse-IoT also allows custom user-defined commands to be integrated with the framework with
:kconfig:option:`CONFIG_INFUSE_RPC_SERVER_USER_COMMANDS`. When enabled, an implementation of
:c:func:`infuse_rpc_server_user_command_runner` must be provided in the application. An example
implementation of this feature can be found in ``infuse-sdk/tests/subsys/rpc/commands/user_commands``.

API Reference
*************

.. doxygengroup:: rpc_packet_headers
.. doxygengroup:: rpc_struct_definitions
.. doxygengroup:: rpc_server_apis
.. doxygengroup:: rpc_commands_apis
.. doxygengroup:: rpc_client_apis
.. doxygengroup:: builtin_rpc_definitions
