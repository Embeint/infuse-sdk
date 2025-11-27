#!/usr/bin/env bash
# Copyright 2024 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

# This script runs the Babblesim CI BT tests.
# It can also be run locally.

export INFUSE_BASE="${INFUSE_BASE:-${PWD}}"
export ZEPHYR_BASE="${ZEPHYR_BASE:-${INFUSE_BASE}/../zephyr}"
export INFUSE_RELATIVE=$(realpath --relative-to=. ${INFUSE_BASE})

set -uex

# nrf52_bsim set:
nice ${INFUSE_BASE}/tests/bsim/bluetooth/compile.sh

RESULTS_FILE=${WORK_DIR}/bsim_results.bt.52.xml \
SEARCH_PATH=${INFUSE_RELATIVE}/tests/bsim/bluetooth \
${ZEPHYR_BASE}/tests/bsim/run_parallel.sh
