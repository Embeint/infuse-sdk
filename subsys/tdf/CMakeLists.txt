# TDF compilation

infuse_sources(
  tdf.c
)
infuse_sources_ifdef(CONFIG_TDF_DIFF
  diffs.c
)
