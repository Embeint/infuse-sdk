#!/usr/bin/env bash
# Copyright 2024 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

# This script runs the Babblesim CI BT tests.
# It can also be run locally.
# Note it will produce its output in ${INFUSE_BASE}/bsim_bt/

export INFUSE_BASE="${INFUSE_BASE:-${PWD}}"
export ZEPHYR_BASE="${ZEPHYR_BASE:-${INFUSE_BASE}/../zephyr}"
cd ${INFUSE_BASE}

set -uex

# nrf52_bsim set:
nice tests/bsim/bluetooth/compile.sh

RESULTS_FILE=${INFUSE_BASE}/bsim_out/bsim_results.bt.52.xml \
SEARCH_PATH=tests/bsim/bluetooth \
${ZEPHYR_BASE}/tests/bsim/run_parallel.sh
