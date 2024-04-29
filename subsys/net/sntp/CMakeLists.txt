# Automatic SNTP time synchronisation

zephyr_sources_ifdef(CONFIG_SNTP_AUTO auto_sntp.c)
