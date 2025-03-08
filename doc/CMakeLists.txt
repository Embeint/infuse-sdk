# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
project(Zephyr-Kernel-Doc LANGUAGES)

set(MIN_WEST_VERSION 1.0.0)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../zephyr" ZEPHYR_BASE)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/../" INFUSE_BASE)

find_package(Zephyr REQUIRED HINTS ${ZEPHYR_BASE} .. COMPONENTS doc)

set(ZBINDINGS ${ZEPHYR_BASE}/dts/bindings)

message(STATUS "Zephyr base: ${ZEPHYR_BASE}")
message(STATUS "Infuse base: ${INFUSE_BASE}")

#-------------------------------------------------------------------------------
# Options

list(APPEND ZEPHYR_ROOT_BINDINGS "${ZBINDINGS}/battery/battery.yaml")
list(APPEND ZEPHYR_ROOT_BINDINGS "${ZBINDINGS}/spi/spi-device.yaml")
list(APPEND ZEPHYR_ROOT_BINDINGS "${ZBINDINGS}/i2c/i2c-device.yaml")
list(APPEND ZEPHYR_ROOT_BINDINGS "${ZBINDINGS}/fuel-gauge/fuel-gauge.yaml")
list(APPEND ZEPHYR_ROOT_BINDINGS "${ZBINDINGS}/sensor/sensor-device.yaml")

set(SPHINXOPTS "-j auto -W --keep-going -T" CACHE STRING "Default Sphinx Options")
set(SPHINXOPTS_EXTRA "" CACHE STRING "Extra Sphinx Options (added to defaults)")
set(LATEXMKOPTS "-halt-on-error -no-shell-escape" CACHE STRING "Default latexmk options")
set(DT_TURBO_MODE OFF CACHE BOOL "Enable DT turbo mode")
set(DOC_TAG "development" CACHE STRING "Documentation tag")
set(DTS_ROOTS "${INFUSE_BASE}" CACHE STRING "DT bindings root folders")
set(DTS_EXTRA "${ZBINDINGS}/base" CACHE STRING "DT bindings extra folders")
set(DTS_EXTRA_FILE "${ZEPHYR_ROOT_BINDINGS}" CACHE STRING "DT bindings extra files")

separate_arguments(SPHINXOPTS)
separate_arguments(SPHINXOPTS_EXTRA)
separate_arguments(LATEXMKOPTS)

#-------------------------------------------------------------------------------
# Dependencies

find_package(Doxygen REQUIRED dot)

find_program(SPHINXBUILD sphinx-build)
if(NOT SPHINXBUILD)
  message(FATAL_ERROR "The 'sphinx-build' command was not found")
endif()
find_program(SPHINXAUTOBUILD sphinx-autobuild)

#-------------------------------------------------------------------------------
# Environment & Paths

set(SPHINX_ENV
  DOXYGEN_EXECUTABLE=${DOXYGEN_EXECUTABLE}
  DOT_EXECUTABLE=${DOXYGEN_DOT_EXECUTABLE}
  DOCS_HTML_DIR=${CMAKE_CURRENT_BINARY_DIR}/html
)

set(DOCS_README ${CMAKE_CURRENT_LIST_DIR}/../README.rst)
set(DOCS_CFG_DIR ${CMAKE_CURRENT_LIST_DIR})
set(DOCS_DOCTREE_DIR ${CMAKE_CURRENT_BINARY_DIR}/doctrees)
set(DOCS_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(DOCS_SRC_DIR ${CMAKE_CURRENT_BINARY_DIR}/src)
set(DOCS_HTML_DIR ${CMAKE_CURRENT_BINARY_DIR}/html)
set(DOCS_LINKCHECK_DIR ${CMAKE_CURRENT_BINARY_DIR}/linkcheck)
set(DOCS_LATEX_DIR ${CMAKE_CURRENT_BINARY_DIR}/latex)

if(WIN32)
  set(SEP $<SEMICOLON>)
else()
  set(SEP :)
endif()

#-------------------------------------------------------------------------------
# Functions

# Create a custom doc target.
#
# This function has the same signature as `add_custom_target()`
#
# The function will create two targets for the doc build system.
# - Target 1 named: `<name>`
# - Target 2 named: `<name>-nodeps`
#
# Both targets will produce same result, but target 2 must have no dependencies.
# This is useful to, e.g. re-run the Sphinx build without dependencies such as
# devicetree generator.
#
function(add_doc_target name)
  add_custom_target(${name} ${ARGN})
  add_custom_target(${name}-nodeps ${ARGN})
endfunction()

#-------------------------------------------------------------------------------
# Doxygen (standalone)

set(DOXY_OUT ${CMAKE_CURRENT_BINARY_DIR}/doxygen)
set(DOXYFILE_IN ${CMAKE_CURRENT_LIST_DIR}/infuse.doxyfile.in)
set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/infuse.doxyfile)
set(ZEPHYR_VERSION "${Zephyr_VERSION}")

configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

add_custom_target(
  doxygen
  COMMAND
    ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
  COMMENT "Running Doxygen..."
)

set_target_properties(
  doxygen
  PROPERTIES
    ADDITIONAL_CLEAN_FILES "${DOXY_OUT}"
)

#-------------------------------------------------------------------------------
# devicetree

set(GEN_DEVICETREE_REST_SCRIPT ${ZEPHYR_BASE}/doc/_scripts/gen_devicetree_rest.py)

set(DTS_ARGS)
foreach(root ${DTS_ROOTS})
  list(APPEND DTS_ARGS --dts-root ${root})
endforeach()
foreach(folder ${DTS_EXTRA})
list(APPEND DTS_ARGS --dts-folder ${folder})
endforeach()
foreach(file ${DTS_EXTRA_FILE})
list(APPEND DTS_ARGS --dts-file ${file})
endforeach()

if(DT_TURBO_MODE)
  list(APPEND DTS_ARGS --turbo-mode)
endif()

add_custom_target(
  devicetree
  COMMAND ${CMAKE_COMMAND} -E env
  PYTHONPATH=${ZEPHYR_BASE}/scripts/dts/python-devicetree/src${SEP}$ENV{PYTHONPATH}
  ZEPHYR_BASE=${ZEPHYR_BASE}
  ${PYTHON_EXECUTABLE} ${GEN_DEVICETREE_REST_SCRIPT}
    --vendor-prefixes ${ZEPHYR_BASE}/dts/bindings/vendor-prefixes.txt
    ${DTS_ARGS}
    ${DOCS_SRC_DIR}/build/dts/api
  VERBATIM
  USES_TERMINAL
  COMMENT "Generating Devicetree bindings documentation..."
)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GEN_DEVICETREE_REST_SCRIPT})

#-------------------------------------------------------------------------------
# html

add_doc_target(
  html
  COMMAND ${CMAKE_COMMAND} -E env ${SPHINX_ENV} OUTPUT_DIR=${DOCS_HTML_DIR}
  ${SPHINXBUILD}
    -b html
    -c ${DOCS_CFG_DIR}
    -d ${DOCS_DOCTREE_DIR}
    -w ${DOCS_BUILD_DIR}/html.log
    -t ${DOC_TAG}
    ${SPHINXOPTS}
    ${SPHINXOPTS_EXTRA}
    ${DOCS_SRC_DIR}
    ${DOCS_HTML_DIR}
  USES_TERMINAL
  COMMENT "Running Sphinx HTML build..."
)

set_target_properties(
  html html-nodeps
  PROPERTIES
    ADDITIONAL_CLEAN_FILES "${DOCS_SRC_DIR};${DOCS_HTML_DIR};${DOCS_DOCTREE_DIR}"
)

add_dependencies(html devicetree)

#-------------------------------------------------------------------------------
# html-live

add_doc_target(
  html-live
  COMMAND ${CMAKE_COMMAND} -E env ${SPHINX_ENV} OUTPUT_DIR=${DOCS_HTML_DIR}
  ${SPHINXAUTOBUILD}
    --watch ${DOCS_README}
    --watch ${DOCS_CFG_DIR}
    --watch ${DOCS_CFG_DIR}/../snippets
    --ignore ${DOCS_BUILD_DIR}
    -b html
    -c ${DOCS_CFG_DIR}
    -d ${DOCS_DOCTREE_DIR}
    -w ${DOCS_BUILD_DIR}/html.log
    -t ${DOC_TAG}
    ${SPHINXOPTS}
    ${SPHINXOPTS_EXTRA}
    ${DOCS_SRC_DIR}
    ${DOCS_HTML_DIR}
  USES_TERMINAL
  COMMENT "Running Sphinx HTML autobuild..."
)

set_target_properties(
  html-live html-live-nodeps
  PROPERTIES
    ADDITIONAL_CLEAN_FILES "${DOCS_SRC_DIR};${DOCS_HTML_DIR};${DOCS_DOCTREE_DIR}"
)

add_dependencies(html-live devicetree)

#-------------------------------------------------------------------------------
# linkcheck

add_doc_target(
  linkcheck
  COMMAND ${CMAKE_COMMAND} -E env ${SPHINX_ENV} OUTPUT_DIR=${DOCS_LINKCHECK_DIR}
  ${SPHINXBUILD}
    -b linkcheck
    -c ${DOCS_CFG_DIR}
    -d ${DOCS_DOCTREE_DIR}
    -w ${DOCS_BUILD_DIR}/linkcheck.log
    -t ${DOC_TAG}
    ${SPHINXOPTS}
    ${SPHINXOPTS_EXTRA}
    ${DOCS_SRC_DIR}
    ${DOCS_LINKCHECK_DIR}
  USES_TERMINAL
  COMMENT "Running Sphinx link check..."
)

set_target_properties(
  linkcheck linkcheck-nodeps
  PROPERTIES
    ADDITIONAL_CLEAN_FILES "${DOCS_SRC_DIR};${DOCS_LINKCHECK_DIR};${DOCS_DOCTREE_DIR}"
)

add_dependencies(linkcheck devicetree)

#-------------------------------------------------------------------------------
# others

add_custom_target(
  pristine
  COMMAND ${CMAKE_COMMAND} -P ${ZEPHYR_BASE}/cmake/pristine.cmake
)
