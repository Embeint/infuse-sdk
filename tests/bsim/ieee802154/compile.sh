#!/usr/bin/env bash
# Copyright 2025 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: Apache-2.0

# Compile all the applications needed by the bsim tests in these subfolders

# set -x #uncomment this line for debugging
set -ue

: "${ZEPHYR_BASE:?ZEPHYR_BASE must be set to point to the zephyr root directory}"
: "${INFUSE_BASE:?INFUSE_BASE must be set to point to the Infuse-IoT root directory}"

#Set a default value to BOARD if it does not have one yet
BOARD="${BOARD:-nrf52_bsim/native}"
OPTS="-iv --no-clean --outdir bsim/ieee802154 -p ${BOARD}"

west twister $OPTS -T ${INFUSE_BASE}/tests/bsim/ieee802154/
