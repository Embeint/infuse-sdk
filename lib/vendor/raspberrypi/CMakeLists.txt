# SPDX-License-Identifier: FSL-1.1-ALv2

infuse_sources_ifndef(CONFIG_INFUSE_CUSTOM_IDENTIFIERS identifiers.c)

if(CONFIG_SOC_SERIES_RP2040 AND CONFIG_MBEDTLS AND NOT CONFIG_CSPRNG_ENABLED)
  message(WARNING
    "RP2040 has no cryptographically secure entropy source. Infuse features "
    "remain enabled, but the default Mbed TLS legacy RNG uses Zephyr's insecure "
    "timer-based test RNG. Key generation and protocol security require a "
    "secure entropy source. The ROSC driver does not provide one."
  )
endif()
