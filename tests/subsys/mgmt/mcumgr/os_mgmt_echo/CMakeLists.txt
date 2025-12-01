#
# Copyright (c) 2022 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.20.0)

# Infuse-IoT application
if(NOT infuse IN_LIST SNIPPET)
  set(SNIPPET infuse ${SNIPPET} CACHE STRING "" FORCE)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(os_mgmt_infuse_echo)

FILE(GLOB app_sources
	src/*.c
)

target_sources(app PRIVATE ${app_sources})
target_include_directories(app PRIVATE ${ZEPHYR_BASE}/subsys/mgmt/mcumgr/transport/include/mgmt/mcumgr/transport/)
target_include_directories(app PRIVATE ${ZEPHYR_BASE}/subsys/mgmt/mcumgr/grp/os_mgmt/include/)
