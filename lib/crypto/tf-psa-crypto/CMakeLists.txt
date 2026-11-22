# tf-psa-crypto integration

if(CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR)
  # Inject the "psa_external_integration_primitives.h" header
  add_library(oberon_accel INTERFACE)
  target_include_directories(oberon_accel INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
  target_link_libraries(zephyr_interface INTERFACE oberon_accel)
  # Actual integration
  infuse_sources(oberon_accelerator.c)
  infuse_sources_ifdef(CONFIG_TF_PSA_CRYPTO_CRACEN_CHACHA20_POLY1305 cracen_chachapoly.c)
  infuse_sources_ifdef(CONFIG_TF_PSA_CRYPTO_CRACEN_HASH cracen_hash.c)
  infuse_sources_ifdef(CONFIG_TF_PSA_CRYPTO_CRACEN_SHA2 cracen_sha2.c)
  infuse_sources_ifdef(CONFIG_TF_PSA_CRYPTO_CRACEN_SHA3 cracen_sha3.c)
  if(CONFIG_TF_PSA_CRYPTO_CRACEN_CHACHA20_POLY1305 OR CONFIG_TF_PSA_CRYPTO_CRACEN_HASH)
    infuse_sources(cracen_cm.c)
  endif()
endif()
