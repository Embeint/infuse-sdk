#!/usr/bin/env bash
# Copyright 2024 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: Apache-2.0

# Compile all the applications needed by the bsim tests in these subfolders

# set -x #uncomment this line for debugging
set -ue

: "${ZEPHYR_BASE:?ZEPHYR_BASE must be set to point to the zephyr root directory}"
: "${INFUSE_BASE:?INFUSE_BASE must be set to point to the Infuse-IoT root directory}"

source ${ZEPHYR_BASE}/tests/bsim/compile.source

APP=tests/bsim/serial/epacket

app_root=$INFUSE_BASE app=$APP conf_file=prj_device.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP conf_file=prj_device_pm.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP conf_file=prj_device_int_single.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP conf_file=prj_device_int_single_pm.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP conf_file=prj_device_async.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP conf_file=prj_device_async_pm.conf snippet=infuse compile

wait_for_background_jobs
