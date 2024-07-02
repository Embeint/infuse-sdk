# Infuse Task Runner compilation

zephyr_library()
zephyr_library_sources(
  runner.c
  schedule.c
)
zephyr_library_sources_ifdef(CONFIG_TASK_RUNNER_AUTO_ITERATE auto_iterate.c)

add_subdirectory(tasks)
