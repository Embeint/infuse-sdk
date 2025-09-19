# Nordic LTE modem library integration

zephyr_sources(nrf_modem_monitor.c)

if(CONFIG_INFUSE_NRF_MODEM_LIB_SIM)
  # ${NRFXLIB_DIR} is unavailable due to the cmake evaluation order
  # This could be resolved with `depends: nrfxlib` in module.yml, but this causes other
  # Kconfig priority issues.
  zephyr_include_directories(${INFUSE_DIR}/../modules/nrfconnect/nrfxlib/nrf_modem/include)
  zephyr_sources(nrf_modem_lib_sim.c)

  zephyr_linker_sources(RODATA nrf_modem_lib.ld)
endif()
