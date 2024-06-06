.. _documentation_generation:

Documentation Generation
########################


Installing the documentation processors
***************************************

Our documentation processing has been tested to run with:

* Doxygen version 1.8.13
* Graphviz 2.43
* Latexmk version 4.56
* Mscgen version 0.20
* All Python dependencies listed in the repository file
  ``doc/requirements.txt``


In order to install the documentation tools, first install Zephyr documentation generationtools as
described in `Zephyr Documentation Generation <https://docs.zephyrproject.org/latest/contribute/documentation/generation.html>`_. Then install additional tools
that are only required to generate the documentation,
as described below:

.. doc_processors_installation_start

.. tabs::

   .. group-tab:: Linux

    Here are the following instructions to install additional tools to build the documentation on Linux:

    On Ubuntu Linux:

    .. code-block:: console

        sudo apt install msc-generator

   .. group-tab:: macOS

      Use ``brew`` to install additional tools to build the documentation on macOS:

      .. code-block:: console

         brew install mscgen

   .. group-tab:: Windows


      Open a ``cmd.exe`` window as **Administrator** and run the following command:

      .. code-block:: console

         choco install 

.. doc_processors_installation_end
