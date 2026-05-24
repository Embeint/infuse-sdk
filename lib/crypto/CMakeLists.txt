# Crypto libraries compilation

add_subdirectory_ifdef(CONFIG_CRYPTO_ASCON ascon)
add_subdirectory_ifdef(CONFIG_CRYPTO_XOODYAK XKCP)
