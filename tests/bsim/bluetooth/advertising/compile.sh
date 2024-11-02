#!/usr/bin/env bash
# Copyright 2024 Embeint Inc
# SPDX-License-Identifier: Apache-2.0

# Compile all the applications needed by the bsim tests in these subfolders

# set -x #uncomment this line for debugging
set -ue

: "${ZEPHYR_BASE:?ZEPHYR_BASE must be set to point to the zephyr root directory}"
: "${INFUSE_BASE:?INFUSE_BASE must be set to point to the Infuse-IoT root directory}"

source ${ZEPHYR_BASE}/tests/bsim/compile.source

APP=tests/bsim/bluetooth/advertising/epacket

app_root=$INFUSE_BASE app=$APP conf_file=prj_advertiser.conf snippet=infuse compile
app_root=$INFUSE_BASE app=$APP conf_file=prj_scanner.conf snippet=infuse compile

wait_for_background_jobs
