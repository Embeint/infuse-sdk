# Infuse-IoT Security compilation

zephyr_library()
zephyr_library_sources(security.c)

add_subdirectory(hardware_unique_key)

# Cloud configuration header generation from .yaml
function(infuse_cloud_config_gen INPUT_FILE OUTPUT_FILE)
  set(DEFAULT_CONFIG_FILE ${CMAKE_CURRENT_LIST_DIR}/default_cloud_config.yaml)

  if(IS_ABSOLUTE ${INPUT_FILE})
    set(CONFIG_FILE ${INPUT_FILE})
  elseif(EXISTS ${CMAKE_CURRENT_LIST_DIR}/${INPUT_FILE})
    set(CONFIG_FILE ${CMAKE_CURRENT_LIST_DIR}/${INPUT_FILE})
  elseif(EXISTS ${APPLICATION_SOURCE_DIR}/${INPUT_FILE})
    set(CONFIG_FILE ${APPLICATION_SOURCE_DIR}/${INPUT_FILE})
  else()
    message(FATAL_ERROR "Cannot find file ${INPUT_FILE}, please provide absolute path")
  endif()
  message("Infuse-IoT cloud configuration: ${CONFIG_FILE}")

  add_custom_command(
    OUTPUT ${OUTPUT_FILE}
    COMMAND
    ${PYTHON_EXECUTABLE}
    ${CMAKE_CURRENT_LIST_DIR}/cloud_config_gen.py
    --config ${CONFIG_FILE}
    --default-config ${DEFAULT_CONFIG_FILE}
    > ${OUTPUT_FILE}
    DEPENDS ${CONFIG_FILE} ${DEFAULT_CONFIG_FILE} ${CMAKE_CURRENT_LIST_DIR}/cloud_config_gen.py
  )
  # Header is public, ensure it exists before any Zephyr library (including app) compiles
  add_custom_target(infuse_cloud_config_h DEPENDS ${OUTPUT_FILE})
  add_dependencies(zephyr_interface infuse_cloud_config_h)
  zephyr_library_sources(${OUTPUT_FILE})
endfunction()

infuse_cloud_config_gen(
  "${CONFIG_INFUSE_SECURITY_CLOUD_CONFIG_FILE}"
  ${ZEPHYR_BINARY_DIR}/include/generated/infuse/cloud_config.h
)

# Network key generation from .yaml
function(infuse_network_keygen INPUT_FILE OUTPUT_FILE VAR_NAME)
  if("${INPUT_FILE}" STREQUAL "")
    return()
  endif()

  if(IS_ABSOLUTE ${INPUT_FILE})
    set(KEY_FILE ${INPUT_FILE})
  else()
    if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/${INPUT_FILE})
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
    DEPENDS ${KEY_FILE} ${CMAKE_CURRENT_LIST_DIR}/keygen.py
  )
  # Header is public, ensure it exists before any Zephyr library (including app) compiles
  add_custom_target(infuse_network_key_${VAR_NAME}_h DEPENDS ${OUTPUT_FILE})
  add_dependencies(zephyr_interface infuse_network_key_${VAR_NAME}_h)
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
