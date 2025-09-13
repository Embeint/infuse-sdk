#!/usr/bin/env bash
# Copyright (c) 2025 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: Apache-2.0

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

simulation_id="bt_scanner_defer_filter"
verbosity_level=2

cd ${BSIM_OUT_PATH}/bin

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_task_runner_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=0 -RealEncryption=0 \
  -testid=bt_scanner_defer_filter -rs=10

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_device_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=1 -RealEncryption=0 \
  -testid=epacket_bt_device -rs=20

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_device_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=2 -RealEncryption=0 \
  -testid=epacket_bt_device -rs=21

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_device_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=3 -RealEncryption=0 \
  -testid=epacket_bt_device -rs=22

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_device_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=4 -RealEncryption=0 \
  -testid=epacket_bt_device -rs=23

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_device_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=5 -RealEncryption=0 \
  -testid=epacket_bt_device -rs=24

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} \
  -D=6 -sim_length=10e6 $@

wait_for_background_jobs
