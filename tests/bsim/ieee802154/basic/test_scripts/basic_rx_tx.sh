#!/usr/bin/env bash
# Copyright (c) 2024 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: Apache-2.0

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

simulation_id="ieee802154_basic"
verbosity_level=2

cd ${BSIM_OUT_PATH}/bin

# Setup sim with two devices ensure they both can hear each other

Execute ./bs_${BOARD_TS}_tests_bsim_ieee802154_basic_prj_802154_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=0 -RealEncryption=0 \
  -testid=ieee802154_device -rs=23

Execute ./bs_${BOARD_TS}_tests_bsim_ieee802154_basic_prj_802154_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=1 -RealEncryption=0 \
  -testid=ieee802154_device -rs=24

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} \
  -D=2 -sim_length=10e6 $@

wait_for_background_jobs
