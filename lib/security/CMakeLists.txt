# Infuse-IoT Security compilation

zephyr_library()
zephyr_library_sources(security.c)

add_subdirectory(hardware_unique_key)

# Network key generation from .yaml
function(infuse_network_keygen INPUT_FILE OUTPUT_FILE VAR_NAME)
  if("${INPUT_FILE}" STREQUAL "")
    return()
  endif()

  if(IS_ABSOLUTE ${INPUT_FILE})
    set(KEY_FILE ${INPUT_FILE})
  else()
    if (EXISTS ${CMAKE_CURRENT_LIST_DIR}/${INPUT_FILE})
      # Default network key
      message(WARNING "Application built with default (insecure) network key")
      set(KEY_FILE ${CMAKE_CURRENT_LIST_DIR}/${INPUT_FILE})
    elseif(EXISTS ${APPLICATION_SOURCE_DIR}/${INPUT_FILE})
      # Key path relative to application directory
      set(KEY_FILE ${APPLICATION_SOURCE_DIR}/${INPUT_FILE})
    else()
      message(FATAL_ERROR "Cannot find file ${INPUT_FILE}, please provide absolute path")
    endif()
  endif()
  message("Infuse-IoT network key: ${KEY_FILE}")

  add_custom_command(
    OUTPUT ${OUTPUT_FILE}
    COMMAND
    ${PYTHON_EXECUTABLE}
    ${CMAKE_CURRENT_LIST_DIR}/keygen.py
    --key ${KEY_FILE}
    --name ${VAR_NAME}
    > ${OUTPUT_FILE}
    DEPENDS ${KEY_FILE}
  )
  zephyr_library_sources(${OUTPUT_FILE})
endfunction()

infuse_network_keygen(
  "${CONFIG_INFUSE_SECURITY_DEFAULT_NETWORK}"
  ${ZEPHYR_BINARY_DIR}/include/generated/infuse/network_key.h
  "infuse"
)
infuse_network_keygen(
  "${CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK}"
  ${ZEPHYR_BINARY_DIR}/include/generated/infuse/network_key_secondary.h
  "secondary"
)
