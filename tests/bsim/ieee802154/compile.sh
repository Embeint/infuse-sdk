#!/usr/bin/env bash
# Copyright 2025 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: Apache-2.0

# Compile all the applications needed by the bsim tests in these subfolders

# set -x #uncomment this line for debugging
set -ue

: "${ZEPHYR_BASE:?ZEPHYR_BASE must be set to point to the zephyr root directory}"
: "${INFUSE_BASE:?INFUSE_BASE must be set to point to the Infuse-IoT root directory}"

source ${ZEPHYR_BASE}/tests/bsim/compile.source

APP_BASIC=tests/bsim/ieee802154/basic

app_root=$INFUSE_BASE app=$APP_BASIC conf_file=prj_802154.conf snippet=infuse compile

wait_for_background_jobs
