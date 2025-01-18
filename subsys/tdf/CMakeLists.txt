# TDF compilation

zephyr_sources(
  tdf.c
)
zephyr_sources_ifdef(CONFIG_TDF_DIFF
  diffs.c
)
