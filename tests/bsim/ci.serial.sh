#!/usr/bin/env bash
# Copyright 2024 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

# This script runs the Babblesim CI serial tests.
# It can also be run locally.

export INFUSE_BASE="${INFUSE_BASE:-${PWD}}"
export ZEPHYR_BASE="${ZEPHYR_BASE:-${INFUSE_BASE}/../zephyr}"
cd ${INFUSE_BASE}

set -uex

# nrf52_bsim set:
nice tests/bsim/serial/compile.sh

RESULTS_FILE=${WORK_DIR}/bsim_results.serial.52.xml \
SEARCH_PATH=tests/bsim/serial \
${ZEPHYR_BASE}/tests/bsim/run_parallel.sh
