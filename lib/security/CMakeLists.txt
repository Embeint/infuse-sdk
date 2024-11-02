# Infuse-IoT Security compilation

zephyr_library()
zephyr_library_sources(security.c)

add_subdirectory(hardware_unique_key)

# Network key generation from .yaml
if(IS_ABSOLUTE ${CONFIG_INFUSE_SECURITY_DEFAULT_NETWORK})
  set(KEY_FILE ${CONFIG_INFUSE_SECURITY_DEFAULT_NETWORK})
else()
  set(KEY_FILE ${CMAKE_CURRENT_LIST_DIR}/${CONFIG_INFUSE_SECURITY_DEFAULT_NETWORK})
endif()
message("Infuse-IoT network key: ${KEY_FILE}")

set(GENERATED_NETWORK_KEY ${ZEPHYR_BINARY_DIR}/include/generated/infuse/network_key.h)
add_custom_command(
  OUTPUT ${GENERATED_NETWORK_KEY}
  COMMAND
  ${PYTHON_EXECUTABLE}
  ${CMAKE_CURRENT_LIST_DIR}/keygen.py
  --key ${KEY_FILE}
  > ${GENERATED_NETWORK_KEY}
  DEPENDS ${KEY_FILE}
)
zephyr_library_sources(${GENERATED_NETWORK_KEY})
