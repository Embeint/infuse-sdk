.. _getting-started-embedded:

Embedded Getting Started Guide
##############################

Follow this guide to:

- Set up a command-line Infuse-IoT Embedded development environment.
- Get the Infuse-IoT Embedded SDK source code.
- Build, flash, and run a sample application.
- Setup development tools in Visual Studio Code.

.. _host_setup:

Setup and Install `Zephyr RTOS <https://www.zephyrproject.org/>`_
*****************************************************************

.. toctree::
    :maxdepth: 1
    :hidden:

    wsl.rst

The first step is to setup and install Zephyr RTOS.
This step can be skipped if Zephyr RTOS 4.0 or later is already installed and working on the machine.

Install and test Zephyr is working by building and flashing the Blinky sample application by following the instructions
in the `Zephyr Getting Started Guide <zephyr_started_>`_ for your respective OS.

.. note::

    When installing the Zephyr SDK, the installer will prompt you to install optional host tools and toolchains.
    Different build targets will require different toolchains.
    See: `Installing the correct Toolchains`_ for more details.

.. note::

    When flashing the Blinky sample, you may need appropriate programmer tools for your microcontroller.
    See: `Install Programmer and Microcontroller specific tools`_ for more details.

.. note::

    Developing on Windows has two options: Native development or WSL (Windows Subsystem for Linux).
    Both options are valid for installation, however not all tools have been tested to work natively on Windows.

    For more details using WSL see the :ref:`getting-started-embedded-wsl`.

Installing the correct Toolchains
---------------------------------

While setting up Zephyr SDK, ensure to select the required toolchain(s) for your build targets.
Below are a few relevant ones:

    * ``arm-zephyr-eabi``: For ARM EABI targets such as Nordic nRF and STM32 boards (including QEMU targets).
    * ``x86_64-zephyr-elf``: For x86 posix/native targets (Used for Babblesim, only valid on Linux).

The SDK setup installer can be re-run to add additional toolchains at a later date if needed.

Install Programmer and Microcontroller specific tools
-----------------------------------------------------

Install host tools required for your microcontroller and programmer (if you haven't already).
Below is a list of the tools required for programmers for the microcontrollers supported by Infuse-Iot:

    * Nordic: `nRF Util <nrf_util_>`_
    * STM32: `stm32cubeprog`_
    * Nordic: `nRF Command Line Tools <nrf_cli_tools_>`_ (Deprecated)

Ensure Zephyr's blinky example flashes to the board before continuing.

Install Infuse-IoT Embedded SDK
*******************************

Now that you have Zephyr installed and working, you can setup Infuse-IoT's Embedded SDK. This
will be setup as a separate, standalone west workspace in ``~/infuse``.
A different destination folder can be used, but corresponding commands will need to be updated accordingly.

Setup and Clone out the Infuse-IoT Embedded SDK using:

.. tabs::
    .. group-tab:: bash/zsh

        .. code:: bash

                mkdir ~/infuse
                cd ~/infuse
                west init -m git@github.com:Embeint/infuse-sdk.git
                west update

    .. group-tab:: Windows (PowerShell)

        .. code:: bash

                mkdir ~/infuse
                cd ~/infuse
                west init -m git@github.com:Embeint/infuse-sdk.git
                west update

    .. group-tab:: Windows (CMD)

        .. code:: batch

                mkdir %USERPROFILE%\infuse
                cd %USERPROFILE%\infuse
                west init -m git@github.com:Embeint/infuse-sdk.git
                west update

Finally install any Python requirements:

.. tabs::
    .. group-tab:: bash/zsh

        .. code:: bash

                pip install -U -r ~/infuse/infuse-sdk/scripts/requirements.txt

    .. group-tab:: Windows (PowerShell)

        .. code:: bash

                pip install -U -r ~/infuse/infuse-sdk/scripts/requirements.txt

    .. group-tab:: Windows (CMD)

        .. code:: batch

                pip install -U -r %USERPROFILE%\infuse\infuse-sdk\scripts\requirements.txt

Infuse-IoT should now be installed, next steps include building and flashing an application to
verify everything is working.

Setup Build Directory (Recommended)
***********************************

Setup a custom build directory template based on the board and app by running the following command
**exactly** as it is below (do **not** change or substitute *"board"* or *"app"*)

.. code:: bash

    west config build.dir-fmt "build/{board}/{app}"

This configures the build directory to be formatted as *build/name_of_board/name_of_app*.

For example, building the ``gateway_usb`` application for the ``nrf52840dk`` outputs the
build to ``build/nrf52840dk/nrf52840/gateway_usb``.

By default, Zephyr only keeps track of one project at a time. Switching project (by building a different
app or board) requires users to overwrite the old build directory, causing the loss of previous build files.
By configuring west to use this build directory format, each new project is stored in a separate folder.
As a result, switching between apps or boards preserved previous builds and switching back no longer requires a
full rebuild. This is recommended, but is optional and can be skipped if you prefer to use the default
build directory behaviour.

.. caution::

    Some ``west`` commands automatically detect the build directory. However, using this configuration
    creates multiple build directories. As a result, commands that use the build directory will require
    the build directory to be specified manually.
    This can easily be done supplying the ``-d`` flag (``--build-dir``).

    e.g. flashing a device would have previously just required ``west flash``, and will now require
    ``west flash -d build/nrf52840dk/nrf52840/gateway_usb`` when run from ``~/infuse``.

Build and Flash a Infuse App
****************************

It's now time to build and flash an Infuse-IoT Application.

The ``gateway_usb`` application allows a Zephyr device to communicate with your host system (over serial) and by
functioning as a bluetooth gateway, allows your machine to interact with nearby Infuse devices over Bluetooth. It can
be run on boards with Bluetooth and Serial comms (USB or RTT), which covers many of Nordic's development boards.
The example below uses the Nordic nRF52840 Dev kit (``nrf52840dk``) but similar boards can be used by switching out the
board name with that of your desired board.
To build the ``gateway_usb`` application for the ``nrf52840dk``, run the following command:

.. code:: bash

        west build -b nrf52840dk/nrf52840 infuse-sdk/apps/gateway_usb

.. note::

    Ensure that the current directory is ``~/infuse`` before running the build and subsequent commands.

and flash it with

.. code:: bash

        west flash -d build/nrf52840dk/nrf52840/gateway_usb

.. note::

    It may be helpful to setup the :ref:`tooling_python_tools` to interact with the ``gateway_usb`` to test it's working.
    Otherwise if your board has LEDs, one should flash once a second, and additional serial ports should appear when
    connecting the device to a computer.


If the build and flash succeeded, Infuse-IoT Embedded SDK has been successfully installed.
Check out how to setup Visual Studio Code or other useful tips further below. If not check out the troubleshooting
section.

Troubleshooting
***************

- Ensure to build applications from inside ``~/infuse`` and not from ``~/infuse/infuse-sdk``, or other folders.
- If ``west init`` or ``git clone`` fails, subsequent attempts may fail. Remove the ``.west`` or ``.git``
  directory respectively before retrying.

.. tabs::

    .. group-tab:: macOS

        While building and flashing, you may need to authorise the toolchain to run. This can be
        done by going to System Settings -> Privacy & Security -> General and clicking "Allow"
        and try again. This needs to be done per executable that is used in the build/flash process
        (So several times). Updating the Zephyr SDK or just periodically will reset
        permissions so it all needs to be done all over again. Thanks Apple.

VSCode Setup
************

VS Code is the preferred development environment for Infuse-IoT. It has many integrations with Kconfig.

.. tabs::

    .. group-tab:: Ubuntu

        VSCode can be installed through Software Center, or follow instructions from `here
        <https://code.visualstudio.com/docs/setup/linux>`__.

    .. group-tab:: macOS

        VSCode can be installed via `homebrew: visual-studio-code
        <https://formulae.brew.sh/cask/visual-studio-code>`__.

        .. code:: bash

                brew install --cask visual-studio-code

    .. group-tab:: Windows

        VSCode can be installed through the Microsoft Store or follow the instructions from `here
        <https://code.visualstudio.com/download>`__.


Once installed, open VSCode, select "Open Folder" and navigate to ``~/infuse``.

Check out :ref:`tooling_vscode_integration` for more details and tips.

Useful Tips
***********

General Tips:

- ``~/infuse`` is like a workspace for Zephyr/Infuse-IoT. ``~/infuse/infuse-sdk``
  contains the Infuse-IoT Embedded SDK.
- The Zephyr installation at ``~/zephyrproject`` is no longer required for Infuse-IoT and can be removed if
  you don't need a mainline zephyr workspace.

  .. note::

        Zephyr installs the Python virtual environment under ``~/zephyrproject/zephyr/.venv``.
        Deleting ``~/zephyrproject``, will require re-creating the venv first.
        Do not attempt to move the ``.venv`` since this can lead to issues.
        To recreate the venv to e.g. ``~/infuse/.venv`` (with the ``zephyrproject/.venv`` is activated):

        .. tabs::

            .. group-tab:: bash/zsh

                .. code:: bash

                        pip freeze > ~/infuse/requirements.txt
                        python3 -m venv ~/infuse/.venv
                        source ~/infuse/.venv/bin/activate
                        pip install -r ~/infuse/requirements.txt
                        rm ~/infuse/requirements.txt

            .. group-tab:: Windows (PowerShell)

                .. code:: bash

                        pip freeze > ~/infuse/requirements.txt
                        python3 -m venv ~/infuse/.venv
                        source ~/infuse/.venv/bin/activate
                        pip install -r ~/infuse/requirements.txt
                        rm ~/infuse/requirements.txt

            .. group-tab:: Windows (CMD)

                .. code:: batch

                        pip freeze > %USERPROFILE%\infuse\requirements.txt
                        python3 -m venv %USERPROFILE%\infuse\.venv
                        source %USERPROFILE%\infuse\.venv\bin\activate
                        pip install -r %USERPROFILE%\infuse\requirements.txt
                        rm %USERPROFILE%\infuse\requirements.txt

.. _zephyr_started: https://docs.zephyrproject.org/latest/develop/getting_started/index.html
.. _python: https://docs.zephyrproject.org/latest/develop/getting_started/index.html#get-zephyr-and-install-python-dependencies
.. _infuse_sdk: https://github.com/Embeint/infuse-sdk
.. _zephyr_sdk: https://docs.zephyrproject.org/latest/develop/toolchains/zephyr_sdk.html#toolchain-zephyr-sdk-install
.. _zephyr_sdk_releases: https://github.com/zephyrproject-rtos/sdk-ng/releases
.. _nrf_cli_tools: https://www.nordicsemi.com/Products/Development-tools/nRF-Command-Line-Tools/Download
.. _nrf_util: https://docs.nordicsemi.com/bundle/nrfutil/page/guides/installing.html
.. _stm32cubeprog: https://www.st.com/en/development-tools/stm32cubeprog.html
