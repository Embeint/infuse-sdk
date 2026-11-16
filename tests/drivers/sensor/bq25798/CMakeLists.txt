# SPDX-License-Identifier: FSL-1.1-ALv2
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bq25798)
target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE ${INFUSE_DIR}/drivers/sensor)
