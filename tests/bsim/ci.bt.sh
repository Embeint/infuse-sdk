#!/usr/bin/env bash
# Copyright 2024 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

# This script runs the Babblesim CI BT tests.
# It can also be run locally.

export INFUSE_BASE="${INFUSE_BASE:-${PWD}}"
export ZEPHYR_BASE="${ZEPHYR_BASE:-${INFUSE_BASE}/../zephyr}"

set -uex

# nrf52_bsim set:
nice ${INFUSE_BASE}/tests/bsim/bluetooth/compile.sh

BOARD=nrf52_bsim/native \
RESULTS_FILE=${WORK_DIR}/bsim_results.bt.52.xml \
SEARCH_PATH=${INFUSE_BASE}/tests/bsim/bluetooth \
${ZEPHYR_BASE}/tests/bsim/run_parallel.sh

# nrf54l15bsim/nrf54l15/cpuapp set:
nice ${INFUSE_BASE}/tests/bsim/bluetooth/compile.nrf54l15bsim_nrf54l15_cpuapp.sh

BOARD=nrf54l15bsim/nrf54l15/cpuapp \
RESULTS_FILE=${WORK_DIR}/bsim_results.bt.54.xml \
SEARCH_PATH=${INFUSE_BASE}/tests/bsim/bluetooth \
${ZEPHYR_BASE}/tests/bsim/run_parallel.sh
