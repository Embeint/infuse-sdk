#!/usr/bin/env bash
# Copyright (c) 2025 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: Apache-2.0

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

simulation_id="serial_tx_timeout"
verbosity_level=2

cd ${BSIM_OUT_PATH}/bin

Execute ./bs_${BOARD_TS}_tests_bsim_serial_epacket_prj_device_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=0 -RealEncryption=0 \
  -testid=epacket_serial_tx_timeout -rs=12 \
  -uart0_fifob_rxfile=tx_to_loopback0 -uart0_fifob_txfile=tx_to_loopback0

Execute ./bs_${BOARD_TS}_tests_bsim_serial_epacket_prj_device_pm_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=1 -RealEncryption=0 \
  -testid=epacket_serial_tx_timeout -rs=23 \
  -uart0_fifob_rxfile=tx_to_loopback1 -uart0_fifob_txfile=tx_to_loopback1

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} \
  -D=2 -sim_length=10e6 $@

wait_for_background_jobs
