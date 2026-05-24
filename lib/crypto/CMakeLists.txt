# Crypto libraries compilation

add_subdirectory(tf-psa-crypto)
add_subdirectory_ifdef(CONFIG_CRYPTO_ASCON ascon)
add_subdirectory_ifdef(CONFIG_CRYPTO_XOODYAK XKCP)
