# SPDX-License-Identifier: FSL-1.1-ALv2
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(provisioning_rpi_pico)
target_sources(app PRIVATE src/main.c ../../../lib/vendor/raspberrypi/identifiers.c)
