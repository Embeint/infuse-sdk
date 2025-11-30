#!/usr/bin/env bash
# Copyright 2024 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

# This script runs the Babblesim CI BT tests.
# It can also be run locally.

export INFUSE_BASE="${INFUSE_BASE:-${PWD}}"
export ZEPHYR_BASE="${ZEPHYR_BASE:-${INFUSE_BASE}/../zephyr}"

set -uex

# nrf52_bsim set:
nice ${INFUSE_BASE}/tests/bsim/ieee802154/compile.sh

RESULTS_FILE=${WORK_DIR}/bsim_results.802154.52.xml \
SEARCH_PATH=${INFUSE_BASE}/tests/bsim/ieee802154 \
${ZEPHYR_BASE}/tests/bsim/run_parallel.sh
