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

BOARD=nrf52_bsim/native \
RESULTS_FILE=${WORK_DIR}/bsim_results.bt.52.xml \
SEARCH_PATH=${INFUSE_RELATIVE}/tests/bsim/bluetooth \
${ZEPHYR_BASE}/tests/bsim/run_parallel.sh

# nrf54l15bsim/nrf54l15/cpuapp set:
nice ${INFUSE_BASE}/tests/bsim/bluetooth/compile.nrf54l15bsim_nrf54l15_cpuapp.sh

export EPACKET_TESTS="${INFUSE_BASE}/tests/bsim/bluetooth/epacket/tests_scripts"

# Only run a subset of tests to validate basic functionality
# The full testsuite takes much longer to run, and does not currently pass
# The Zephyr controller has a few bugs with
BOARD=nrf54l15bsim/nrf54l15/cpuapp \
RESULTS_FILE=${WORK_DIR}/bsim_results.bt.54.xml \
TESTS_LIST=\
"${EPACKET_TESTS}/bt_file_copy.sh \
${EPACKET_TESTS}/connect.sh \
${EPACKET_TESTS}/scan.sh \
${EPACKET_TESTS}/legacy_connect.sh \
${EPACKET_TESTS}/mcumgr_reboot.sh" \
${ZEPHYR_BASE}/tests/bsim/run_parallel.sh
