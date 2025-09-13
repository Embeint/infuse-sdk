#!/usr/bin/env bash
# Copyright (c) 2024 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: Apache-2.0

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

simulation_id="epacket_connect_multi"
verbosity_level=2

cd ${BSIM_OUT_PATH}/bin

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_device_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=0 -RealEncryption=0 \
  -testid=epacket_bt_device -rs=23

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_device_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=1 -RealEncryption=0 \
  -testid=epacket_bt_device -rs=15

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_gateway_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=2 -RealEncryption=0 \
  -testid=epacket_bt_gateway_connect_multi -rs=6

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} \
  -D=3 -sim_length=10e6 $@

wait_for_background_jobs
