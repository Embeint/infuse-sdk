.. _python_credentials:

Credentials
###########

The ``credentials`` subcommand manages storage of secrets for the package.
It is a thin wrapper over the python `keyring`_ package.

.. code:: bash

    > infuse credentials --help
    usage: infuse credentials [-h] [--api-key API_KEY] [--network NETWORK]

    Manage Infuse-IoT credentials

    options:
      -h, --help         show this help message and exit
      --api-key API_KEY  Set Infuse-IoT API key
      --network NETWORK  Load network credentials from file

API Key
*******

An API key is required to interact with the Infuse-IoT cloud services, which
includes generating the shared secrets required to run authenticated commands
on devices (see :ref:`platform-security-model`).

.. code:: bash

    infuse credentials --api-key $API_KEY

.. note::

    Substitute your API key for the $API_KEY variable in the above command. The
    API key **MUST NOT** include the ``Bearer`` prefix. API keys currently must
    be requested directly from the Infuse-IoT team.


Network Keys
************

To decode network encrypted packets, the base network key must be loaded into the
credential manager. This only needs to be performed once per install.

.. code:: bash

    infuse credentials --network-key /path/to/network_key.yaml

New network key files can be generated with the ``infuse-sdk/scripts/network_key_gen.py`` helper, which uses a cryptographically secure random number generator.

.. note::

    The default network (ID 0) is loaded by default.


Keyring Debugging
*****************

Particularly on WSL2, the default ``keyring`` backends can have problems when attempting to use
the credentials library. Example errors:

.. code:: bash

    no keyring backend
    keyring.errors.KeyringLocked: Failed to unlock the collection!
    keyring.errors.InitError: Failed to create the collection: Prompt dismissed..

Debugging
=========

Ensure that ``keyring.backends.SecretService.Keyring`` exists as a keyring backend. If it does
not, ensure that ``gnome-keyring`` is installed.

.. code:: bash

    keyring --list-backends
    sudo apt install gnome-keyring

Scorched Earth Reset
====================

 1. Remove ``gnome-keyring``: ``sudo apt remove gnome-keyring``
 2. Remove keychains: ``rm -rf ~/.local/share/keyrings/*``
 3. Reboot WSL: ``wsl --shutdown`` (From powershell)
 4. Reinstall ``gnome-keyring`` and ``seahorse``: ``sudo apt install gnome-keyring seahorse``
 5. Refresh dbus: ``dbus-update-activation-environment --all``
 6. Open seahorse: ``seahorse``
 7. Create a new password for the default keychain at the prompt

.. _keyring: https://pypi.org/project/keyring/
