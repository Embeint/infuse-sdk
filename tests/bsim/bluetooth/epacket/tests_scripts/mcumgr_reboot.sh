#!/usr/bin/env bash
# Copyright (c) 2025 Embeint Inc
# SPDX-License-Identifier: Apache-2.0

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

simulation_id="epacket_mcumgr_reboot"
verbosity_level=2

cd ${BSIM_OUT_PATH}/bin

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_prj_legacy_adv_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=0 -RealEncryption=0 \
  -testid=legacy_adv_expect_reboot -rs=23

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_prj_gateway_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=1 -RealEncryption=0 \
  -testid=epacket_bt_gateway_mcumgr_reboot -rs=6

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} \
  -D=2 -sim_length=10e6 $@

wait_for_background_jobs
