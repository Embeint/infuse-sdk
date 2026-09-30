# SPDX-License-Identifier: FSL-1.1-ALv2
# Select Pico Wi-Fi firmware with the btsdio shared-memory feature, its matching
# CLM and the shared-antenna NVRAM settings. Leave the imported HAL untouched.
function(infuse_cyw43_resources_configure)
  set(firmware_source ${INFUSE_DIR}/zephyr/blobs/img/wb43439A0_7_95_49_00_combined.h)
  set(extractor ${INFUSE_DIR}/drivers/bluetooth/cyw43_firmware.py)
  if(NOT EXISTS ${firmware_source})
    message(FATAL_ERROR "CYW43439 shared-bus firmware missing: run west blobs fetch infuse-sdk")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${firmware_source} ${extractor})
  execute_process(COMMAND ${Python3_EXECUTABLE} ${extractor} ${firmware_source} ${PROJECT_BINARY_DIR}
    RESULT_VARIABLE result OUTPUT_VARIABLE sizes OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Unable to extract CYW43439 firmware")
  endif()
  list(GET sizes 0 fw_size)
  list(GET sizes 1 clm_size)
  set(nvram_source ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/whd-expansion/WHD/COMPONENT_WIFI5/resources/nvram/COMPONENT_43439/COMPONENT_MURATA-1YN/cyfmac43439-1YN.txt)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${nvram_source})
  file(READ ${nvram_source} nvram)
  string(REPLACE "btc_mode=0" "btc_mode=1" nvram "${nvram}")
  string(REPLACE "muxenab=0x11" "muxenab=0x100" nvram "${nvram}")
  set(nvram_path ${PROJECT_BINARY_DIR}/cyw43_pico_nvram.txt)
  file(WRITE ${nvram_path} "${nvram}")
  file(SIZE ${nvram_path} nvram_size)
  get_target_property(definitions zephyr_interface INTERFACE_COMPILE_DEFINITIONS)
  list(FILTER definitions EXCLUDE REGEX "^(NVRAM|FW|CLM)_IMAGE_(NAME|SIZE)=")
  set_property(TARGET zephyr_interface PROPERTY INTERFACE_COMPILE_DEFINITIONS ${definitions})
  # WHD embeds these files through assembler .incbin directives, which the
  # C compiler's dependency scanner cannot see.
  set(resource_source ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/whd-expansion/WHD/COMPONENT_WIFI5/resources/resource_imp/whd_resources.c)
  set_property(SOURCE ${resource_source} TARGET_DIRECTORY zephyr APPEND PROPERTY OBJECT_DEPENDS
    ${PROJECT_BINARY_DIR}/wifi.bin ${PROJECT_BINARY_DIR}/wifi.clm_blob ${nvram_path})
  zephyr_compile_definitions(
    FW_IMAGE_NAME="${PROJECT_BINARY_DIR}/wifi.bin" FW_IMAGE_SIZE=${fw_size}
    CLM_IMAGE_NAME="${PROJECT_BINARY_DIR}/wifi.clm_blob" CLM_IMAGE_SIZE=${clm_size})
  zephyr_compile_definitions(NVRAM_IMAGE_NAME="${nvram_path}" NVRAM_IMAGE_SIZE=${nvram_size})
endfunction()
# Imported modules add their NVRAM definitions after Infuse is processed.
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL infuse_cyw43_resources_configure)
