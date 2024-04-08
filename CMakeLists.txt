# Embedded Intelligence SDK compilation

set(EIS_DIR ${CMAKE_CURRENT_LIST_DIR} CACHE PATH "EIS root directory")

zephyr_include_directories(include)

add_subdirectory(lib)
add_subdirectory(subsys)

if (CONFIG_EIS_SDK)
  if (CONFIG_BUILD_WITH_TFM)
    # PSA algorithm support
    set(EIS_TFM_MBEDTLS_PATH ${EIS_DIR}/include/eis/tfm/mbedtls_config_extra.h)
    set_property(TARGET zephyr_property_target
      APPEND PROPERTY TFM_CMAKE_OPTIONS
      -DTFM_MBEDCRYPTO_PLATFORM_EXTRA_CONFIG_PATH=${EIS_TFM_MBEDTLS_PATH}
    )
  endif()
endif()
