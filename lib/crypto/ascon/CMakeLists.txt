# Infuse-IoT ascon compilation

# Algorithm libraries
if(CONFIG_CRYPTO_ASCON_128)
  zephyr_library_named(crypto_ascon128)
  zephyr_library_include_directories(crypto_aead)
  if (CONFIG_CRYPTO_ASCON_INLINE_MODE)
    zephyr_library_compile_definitions(ASCON_INLINE_MODE)
    zephyr_library_compile_definitions(ASCON_INLINE_PERM)
  endif()
  # Rename symbols
  zephyr_library_compile_definitions(ascon_aead=ascon128_aead)
  zephyr_library_compile_definitions(ascon_update=ascon128_update)
  zephyr_library_compile_definitions(crypto_aead_encrypt=ascon128_aead_encrypt)
  zephyr_library_compile_definitions(crypto_aead_decrypt=ascon128_aead_decrypt)
  # Compile all files from the implementation directory
  file(GLOB ASCON_SRC_FILES crypto_aead/ascon128v12/${CONFIG_CRYPTO_ASCON_IMPL}/*.c)
  zephyr_library_sources(${ASCON_SRC_FILES})
endif()

if(CONFIG_CRYPTO_ASCON_128A)
  zephyr_library_named(crypto_ascon128a)
  zephyr_library_include_directories(crypto_aead)
  if (CONFIG_CRYPTO_ASCON_INLINE_MODE)
    zephyr_library_compile_definitions(ASCON_INLINE_MODE)
    zephyr_library_compile_definitions(ASCON_INLINE_PERM)
  endif()
  # Rename symbols
  zephyr_library_compile_definitions(ascon_aead=ascon128a_aead)
  zephyr_library_compile_definitions(ascon_update=ascon128a_update)
  zephyr_library_compile_definitions(crypto_aead_encrypt=ascon128a_aead_encrypt)
  zephyr_library_compile_definitions(crypto_aead_decrypt=ascon128a_aead_decrypt)
  # Compile all files from the implementation directory
  file(GLOB ASCON_SRC_FILES crypto_aead/ascon128av12/${CONFIG_CRYPTO_ASCON_IMPL}/*.c)
  zephyr_library_sources(${ASCON_SRC_FILES})
endif()

if(CONFIG_CRYPTO_ASCON_80PQ)
  zephyr_library_named(crypto_ascon80pq)
  zephyr_library_include_directories(crypto_aead)
  if (CONFIG_CRYPTO_ASCON_INLINE_MODE)
    zephyr_library_compile_definitions(ASCON_INLINE_MODE)
    zephyr_library_compile_definitions(ASCON_INLINE_PERM)
  endif()
  # Rename symbols
  zephyr_library_compile_definitions(ascon_aead=ascon80pq_aead)
  zephyr_library_compile_definitions(ascon_update=ascon80pq_update)
  zephyr_library_compile_definitions(crypto_aead_encrypt=ascon80pq_aead_encrypt)
  zephyr_library_compile_definitions(crypto_aead_decrypt=ascon80pq_aead_decrypt)
  # Compile all files from the implementation directory
  file(GLOB ASCON_SRC_FILES crypto_aead/ascon80pqv12/${CONFIG_CRYPTO_ASCON_IMPL}/*.c)
  zephyr_library_sources(${ASCON_SRC_FILES})
endif()
