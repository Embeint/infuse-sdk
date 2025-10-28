# SPDX-License-Identifier: Apache-2.0

set(EXTRA_CONF_FILE "../../infuse_security.conf")

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(security)

target_sources(app PRIVATE
  src/main.c
)
