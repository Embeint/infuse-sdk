# Nordic LTE modem library integration

zephyr_library_named(infuse_modem_nrf9x)

zephyr_library_sources(nrf_modem_monitor.c)

if(CONFIG_INFUSE_NRF_MODEM_LIB_SIM)
  # Not scoped to the library, as tests may want to include "nrf_modem.h"
  zephyr_include_directories(${ZEPHYR_NRFXLIB_MODULE_DIR}/nrf_modem/include)
  zephyr_library_sources(nrf_modem_lib_sim.c)

  zephyr_linker_sources(RODATA nrf_modem_lib.ld)
endif()
