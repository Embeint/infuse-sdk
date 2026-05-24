# tf-psa-crypto integration

if(CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR)
  # Inject the "psa_external_integration_primitives.h" header
  add_library(oberon_accel INTERFACE)
  target_include_directories(oberon_accel INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
  target_link_libraries(zephyr_interface INTERFACE oberon_accel)
  # Actual integration
  zephyr_sources(oberon_accelerator.c)
endif()
