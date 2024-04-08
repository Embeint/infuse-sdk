# SDK-NRF helpers

if (NOT CONFIG_BUILD_WITH_TFM)
  if (CONFIG_NRF_OBERON)
    # Link Oberon MbedTLS backend
    add_library(mbedcrypto_common INTERFACE)
    target_link_libraries(mbedcrypto_common INTERFACE
      mbedcrypto_oberon_mbedtls_imported
      mbedcrypto_oberon_imported
    )
    target_include_directories(mbedcrypto_common
      INTERFACE
        # Add nrf_oberon includes
        ${ZEPHYR_NRFXLIB_MODULE_DIR}/crypto/nrf_oberon/include/
        ${ZEPHYR_NRFXLIB_MODULE_DIR}/crypto/nrf_oberon/include/mbedtls
        # Add Oberon PSA Crypto Driver includes
        ${ZEPHYR_SDK_NRF_MODULE_DIR}/ext/oberon/psa/core/library
    )
    zephyr_link_libraries(mbedcrypto_common)
  endif()
endif()
