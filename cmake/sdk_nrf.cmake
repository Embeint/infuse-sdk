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
        ${ZEPHYR_OBERON_PSA_CRYPTO_MODULE_DIR}/library
    )
    # Multiple definitions of mbedtls_ecdsa_can_do from oberon...
    target_compile_definitions(mbedcrypto_common INTERFACE "-Dmbedtls_ecdsa_can_do=_mock_mbedtls_ecdsa_can_do")

    zephyr_link_libraries(mbedcrypto_common)
  endif()
endif()
