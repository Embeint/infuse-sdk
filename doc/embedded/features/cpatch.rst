.. _cpatch_api:

CPatch
######

Constrained Patch (CPatch) is a binary patching algorithm, designed to be as
simple as possible to implement on the patch application side. It is optimised
for patching embedded firmware applications.

Features
********

  * Small ROM footprint (~1200 bytes)
  * Constant size internal patch state
  * Configurable output buffer size
  * Linear patch read pattern
  * Linear output write pattern
  * Integrated input, patch & output data validation
  * No intermediate decompression step
  * Competitive compression ratio

Compression Tool Comparison
***************************

Using patch version increments of several applications as an example,
we can compare the compression ratios of several binary diff tools.

.. list-table:: nRF9151 TF-M application (528kB)
   :header-rows: 1

   * - Algorithm
     - Patch Size (bytes)
     - Patch Ratio
   * - CPatch
     - 36836
     - 6.81%
   * - JojoDiff
     - 49835
     - 9.22%
   * - bsdiff4
     - 15317
     - 2.83%
   * - xdelta
     - 82748
     - 15.31%

.. list-table:: nRF52840 application (320kB)
   :header-rows: 1

   * - Algorithm
     - Patch Size (bytes)
     - Patch Ratio
   * - CPatch
     - 15252
     - 4.66%
   * - JojoDiff
     - 21365
     - 6.53%
   * - bsdiff4
     - 6994
     - 2.14%
   * - xdelta
     - 41259
     - 12.61%

.. note::

    These comparisons can be generated on your own releases with the ``--tool-compare`` argument to
    ``west release-diff`` (The appropriate applications must be installed on ``PATH``).

API Reference
*************

.. doxygengroup:: cpatch_patch_apis
