# Copyright 2024 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bsim_test_bluetooth_epacket)

target_sources(app PRIVATE
    src/common.c
)

if(CONFIG_INFUSE_LEGACY_ADV)
    target_sources(app PRIVATE
        src/legacy_adv.c
    )
elseif(CONFIG_CONN_TERMINATOR)
    target_sources(app PRIVATE
        src/terminator.c
    )
elseif(CONFIG_BT_CENTRAL)
    target_sources(app PRIVATE
        src/gateway.c
    )
else()
    target_sources(app PRIVATE
        src/device.c
    )
endif()

zephyr_include_directories(
  ${BSIM_COMPONENTS_PATH}/libUtilv1/src/
  ${BSIM_COMPONENTS_PATH}/libPhyComv1/src/
)
