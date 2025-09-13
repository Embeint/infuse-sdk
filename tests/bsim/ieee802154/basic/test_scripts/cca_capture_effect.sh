#!/usr/bin/env bash
# Copyright (c) 2024 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: Apache-2.0

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

simulation_id="ieee802154_cca_capture_effect"
verbosity_level=2

cd ${BSIM_OUT_PATH}/bin

# Setup sim with three devices. Validate that a device with the highest
# signal strength out of d0 and d2 can be heard by d1. This is done by
# setting the random seed of d0 and d2 to the same, while being out of range
# to avoid CCA.

Execute ./bs_${BOARD_TS}_tests_bsim_ieee802154_basic_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=0 -RealEncryption=0 \
  -testid=ieee802154_device -rs=25 -argstest rx_count 0

Execute ./bs_${BOARD_TS}_tests_bsim_ieee802154_basic_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=1 -RealEncryption=0 \
  -testid=ieee802154_device -rs=24 -argstest rx_count 1 no_tx

Execute ./bs_${BOARD_TS}_tests_bsim_ieee802154_basic_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=2 -RealEncryption=0 \
  -testid=ieee802154_device -rs=25 -argstest rx_count 0

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} \
  -D=3 -sim_length=10e6 -defmodem=BLE_simple -channel=multiatt -argschannel -at=40 \
  -file=$INFUSE_BASE/tests/bsim/ieee802154/basic/multiatt_symmetric.txt $@

wait_for_background_jobs
