.. _getting-started-embedded-wsl:

Getting Started Guide Windows (WSL)
###################################

Follow this guide to install Infuse-IoT for Windows using WSL (Windows Subsystem for Linux).
This process provides a posix style workspace for using Infuse-IoT, without setting up a virtual machine.

Prerequisites
#############

The first step is to install WSL and a Linux Distribution (Ubuntu preferred).

Follow Microsoft’s guide to `install WSL
<https://learn.microsoft.com/en-us/windows/wsl/install>`_ along with the default
Distribution (Ubuntu).

.. note::

    If using an existing WSL installation, make sure it is using WSL Version 2 (or
    create a new installation) . Follow Microsoft’s `guide to List and Set WSL Versions
    <https://learn.microsoft.com/en-us/windows/wsl/basic-commands#
    list-installed-linux-distributions>`_.

    WSL2 is required to use programmers from within the WSL environment.

After installing and setting up your WSL environment, Install `WSL-USB <wsl_usb>`_. This can be installed via

.. code:: bash

        winget install --interactive --exact dorssel.usbipd-win

This tool is used to allow WSL to access USB devices plugged into the Windows Host such as programmers.
Once installed, the USB programmer will need to be changed from the ``Windows USB Devices`` to the
``Forwarded Devices``.

Install Zephyr and Infuse-IoT
#############################

Next follow the rest of the Infuse-IoT Getting started guide.

.. note::

	Ensure to follow any instructions specific to Ubuntu, not Windows (Native).

VSCode Setup
************

VSCode is the preferred development environment for Infuse-IoT. It has many integrations with Kconfig.
Setting up VSCode with WSL is slightly different.

1. Install `VScode <https://code.visualstudio.com/download>`_ on the Windows Desktop
   (not WSL).
2. Once installed, open `VScode` and click on the bottom right corner with the blue box with the
   white arrows and Select *"Connect to WSL"* and follow the prompts (this may take a
   little while for the first time).
3. Then select "Open Folder" and navigate to ``~/infuse-iot``.

Useful Tips
***********
- You can access the Windows file system from within WSL at ``/mnt/c/`` (or ``/mnt/d/`` etc).
- Similarly, you can access WSL files in Windows at ``\\wsl$``.

.. _wsl_usb: https://github.com/dorssel/usbipd-win/releases/tag/v4.4.0
