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

    When flashing the Blinky sample, you may need appropriate programmer tools for your microcontroller.
    See the next step for more details.

.. note::

    Developing on Windows has two options: Native development or WSL (Windows Subsystem for Linux).
    Both options are valid for installation, however not all tools have been tested to work on Native Windows.

    For more details using WSL see the :ref:`getting-started-embedded-wsl`.

Install Programmer and Microcontroller specific tools
*****************************************************

Install host tools required for your microcontroller and programmer (if you haven't already).
Below is a list of the tools required for programmers for the microcontrollers supported by infuse:

    * nRF: `nRF Command Line Tools <nrf_cli_tools_>`_
    * STM32: `stm32cubeprog`_

.. note::

    nRF Command line tools comes bundled with the `Segger JLink Tools <https://www.segger.com/downloads/jlink/>_`.
    Follow the appropriate prompts to install both.

Ensure the blinky example flashes to the board before continuing.

Install Infuse-IoT Embedded SDK
*******************************

Now that you have Zephyr installed and working, you can setup Infuse-IoT's Embedded SDK. This
will be setup as a standalone west workspace in ``~/infuse-iot``.
(A different destination folder can be used at your own risk)

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

.. note::

    Ensure you have access to `infuse_sdk`_ on GitHub and have added your
    `SSH key <https://github.com/settings/keys>`_ to GitHub.

    If the clone fails (e.g. due to permissions issues), the ``~/infuse-iot/.west``
    folder will likely need to be deleted before trying reattempting. This can be done by running:

    .. tabs::

        .. group-tab:: bash/zsh

            .. code:: bash

                    rm -rf ~/infuse-iot/.west/

        .. group-tab:: Windows (PowerShell)

            .. code:: bash

                    rm -r ~/infuse-iot/.west/

        .. group-tab:: Windows (CMD)

            .. code:: batch

                    rd /s /q %USERPROFILE%\infuse-iot\.west\

Finally install any Python requirements:

.. tabs::
    .. group-tab:: bash/zsh

        .. code:: bash

                pip install -U -r ~/infuse-iot/infuse-sdk/scripts/requirements.txt

    .. group-tab:: Windows (PowerShell)

        .. code:: bash

                pip install -U -r ~/infuse-iot/infuse-sdk/scripts/requirements.txt

    .. group-tab:: Windows (CMD)

        .. code:: batch

                pip install -U -r %USERPROFILE%\infuse-iot\infuse-sdk\scripts\requirements.txt

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

By default, Zephyr attempts to overwrite the old build directory when switching between apps or boards.
This feature is useful because when using this layout, switching between apps or boards no
longer overwrites your existing build directory. When you switch back, the previous build
is preserved and the entire app doesn’t need to be rebuilt from scratch.

.. note::

    This is recommended, but is optional and can be skipped if you prefer to use the default
    build directory.

.. note::

    Some `west` commands automatically detect the build directory.
    Using this configuration will require the build directly to be manually specified.
    This can easily be done using the ``-d`` flag

    e.g. ``west flash -d build/nrf52840dk/nrf52840/gateway_usb`` when run from ``~/infuse-iot``.

Build and Flash a Infuse App
****************************

It's now time to build and flash an Infuse-IoT Application.

For a Nordic nRF52840 Dev kit, you can build and flash the ``gateway_usb`` application.

.. code:: bash

        west build -b nrf52840dk/nrf52840 infuse-sdk/apps/gateway_usb

.. note::

    Ensure you are in the ``~/infuse-iot`` directory.

and flash it with

.. code:: bash

        west flash -d build/nrf52840dk/nrf52840/gateway_usb

.. note::

    It may be helpful to setup the :ref:`tooling_python_tools` to interact with the ``gateway_usb``.

If the build and flash succeeded, Infuse-IoT Embedded SDK has been successfully installed. Check
out how to setup `VScode` or other useful tips further below. If not check out the troubleshooting
section.

Troubleshooting
***************

- Ensure to build applications from inside ``~/infuse-iot`` and not from ``~/infuse-iot/infuse-sdk``, or other folders.
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
*************

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


Once installed, open VSCode, select "Open Folder" and navigate to ``~/infuse-iot``.

Check out :ref:`tooling_vscode_integration` for more details and tips.

Useful Tips
***********

General Tips:

- ``~/infuse-iot`` is like a workspace for Zephyr/Infuse-IoT. ``~/infuse-iot/infuse-sdk``
  contains the Infuse-IoT Embedded SDK.
- The Zephyr installation at ``~/zephyrproject`` is no longer required for Infuse-IoT and can be removed if
  you don't need a mainline zephyr workspace.

  .. note::

        Zephyr installs the Python virtual environment under ``~/zephyrproject/zephyr/.venv``.
        Deleting ``~/zephyrproject``, will require re-creating the venv first.
        Do not attempt to move the ``.venv`` since this can lead to issues.
        To recreate the venv to e.g. ``~/infuse-iot/.venv`` (with the ``zephyrproject/.venv`` already activated):

        .. tabs::

            .. group-tab:: bash/zsh

                .. code:: bash

                        pip freeze > ~/infuse-iot/requirements.txt
                        python3 -m venv ~/infuse-iot/.venv
                        source ~/infuse-iot/.venv/bin/activate
                        pip install -r ~/infuse-iot/requirements.txt
                        rm ~/infuse-iot/requirements.txt

            .. group-tab:: Windows (PowerShell)

                .. code:: bash

                        pip freeze > ~/infuse-iot/requirements.txt
                        python3 -m venv ~/infuse-iot/.venv
                        source ~/infuse-iot/.venv/bin/activate
                        pip install -r ~/infuse-iot/requirements.txt
                        rm ~/infuse-iot/requirements.txt

            .. group-tab:: Windows (CMD)

                .. code:: batch

                        pip freeze > %USERPROFILE%\infuse-iot\requirements.txt
                        python3 -m venv %USERPROFILE%\infuse-iot\.venv
                        source %USERPROFILE%\infuse-iot\.venv\bin\activate
                        pip install -r %USERPROFILE%/infuse-iot\requirements.txt
                        rm %USERPROFILE%\infuse-iot\requirements.txt

.. _zephyr_started: https://docs.zephyrproject.org/latest/develop/getting_started/index.html
.. _python: https://docs.zephyrproject.org/latest/develop/getting_started/index.html#get-zephyr-and-install-python-dependencies
.. _infuse_sdk: https://github.com/Embeint/infuse-sdk
.. _zephyr_sdk: https://docs.zephyrproject.org/latest/develop/toolchains/zephyr_sdk.html#toolchain-zephyr-sdk-install
.. _zephyr_sdk_releases: https://github.com/zephyrproject-rtos/sdk-ng/releases
.. _nrf_cli_tools: https://www.nordicsemi.com/Products/Development-tools/nRF-Command-Line-Tools/Download
.. _stm32cubeprog: https://www.st.com/en/development-tools/stm32cubeprog.html
