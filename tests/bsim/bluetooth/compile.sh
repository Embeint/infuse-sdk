#!/usr/bin/env bash
# Copyright 2024 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: Apache-2.0

# Compile all the applications needed by the bsim tests in these subfolders

# set -x #uncomment this line for debugging
set -ue

: "${ZEPHYR_BASE:?ZEPHYR_BASE must be set to point to the zephyr root directory}"
: "${INFUSE_BASE:?INFUSE_BASE must be set to point to the Infuse-IoT root directory}"

source ${ZEPHYR_BASE}/tests/bsim/compile.source

APP_EPACKET=tests/bsim/bluetooth/epacket
APP_GATT=tests/bsim/bluetooth/gatt
APP_TASK_RUNNER=tests/bsim/bluetooth/task_runner

app_root=$INFUSE_BASE app=$APP_EPACKET conf_file=prj_legacy_adv.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_EPACKET conf_file=prj_legacy_adv_named.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_EPACKET conf_file=prj_conn_terminator.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_EPACKET conf_file=prj_device.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_EPACKET conf_file=prj_device_public.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_EPACKET conf_file=prj_device_alt_network.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_EPACKET conf_file=prj_gateway.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_EPACKET conf_file=prj_gateway_grouping.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_EPACKET conf_file=prj_gateway_knows_alt.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_GATT snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_TASK_RUNNER snippet=infuse compile

wait_for_background_jobs
