# Infuse-IoT SDK compilation

set(INFUSE_DIR ${CMAKE_CURRENT_LIST_DIR} CACHE PATH "Infuse-IoT SDK root directory")

zephyr_include_directories(include)
if (NOT CONFIG_INFUSE_DEFS_GENERATED_DOWNSTREAM)
  zephyr_include_directories(generated/include)
endif()

add_subdirectory(drivers)
add_subdirectory(lib)
add_subdirectory(subsys)

if (CONFIG_INFUSE_SDK)
  if (CONFIG_BUILD_WITH_TFM)
    # PSA algorithm support
    set(INFUSE_TFM_MBEDTLS_PATH ${INFUSE_DIR}/include/infuse/tfm/mbedtls_config_extra.h)
    set_property(TARGET zephyr_property_target
      APPEND PROPERTY TFM_CMAKE_OPTIONS
      -DTFM_MBEDCRYPTO_PLATFORM_EXTRA_CONFIG_PATH=${INFUSE_TFM_MBEDTLS_PATH}
    )
  endif()
endif()

# Required for PSA headers
zephyr_link_libraries_ifdef(CONFIG_MBEDTLS mbedTLS)

include(cmake/sdk_nrf.cmake)
