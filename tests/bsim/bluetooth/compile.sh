#!/usr/bin/env bash
# Copyright 2024 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: Apache-2.0

# Compile all the applications needed by the bsim tests in these subfolders

# set -x #uncomment this line for debugging
set -ue

: "${ZEPHYR_BASE:?ZEPHYR_BASE must be set to point to the zephyr root directory}"
: "${INFUSE_BASE:?INFUSE_BASE must be set to point to the Infuse-IoT root directory}"

source ${ZEPHYR_BASE}/tests/bsim/compile.source

APP_DEVICE=tests/bsim/bluetooth/epacket/device
APP_GATEWAY=tests/bsim/bluetooth/epacket/gateway
APP_LEGACY_ADV=tests/bsim/bluetooth/epacket/legacy_adv
APP_TERMINATOR=tests/bsim/bluetooth/epacket/terminator
APP_GATT=tests/bsim/bluetooth/gatt
APP_TASK_RUNNER=tests/bsim/bluetooth/task_runner

app_root=$INFUSE_BASE app=$APP_LEGACY_ADV conf_file=prj.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_LEGACY_ADV conf_file=prj_named.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_TERMINATOR conf_file=prj.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_DEVICE conf_file=prj.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_DEVICE conf_file=prj_public.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_DEVICE conf_file=prj_alt_network.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_GATEWAY conf_file=prj.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_GATEWAY conf_file=prj_grouping.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_GATEWAY conf_file=prj_knows_alt.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_GATT snippet=infuse compile
app_root=$INFUSE_BASE app=$APP_TASK_RUNNER snippet=infuse compile

wait_for_background_jobs
