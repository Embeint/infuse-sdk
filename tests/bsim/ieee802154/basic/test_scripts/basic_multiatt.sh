#!/usr/bin/env bash
# Copyright (c) 2024 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: Apache-2.0

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

simulation_id="ieee802154_multiatt"
verbosity_level=2

cd ${BSIM_OUT_PATH}/bin

# Setup sim with 3 devices. Ensure only d1 can hear both d0 and d2

Execute ./bs_${BOARD_TS}_tests_bsim_ieee802154_basic_prj_802154_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=0 -RealEncryption=0 \
  -testid=ieee802154_device -rs=23 -argstest rx_count 1

Execute ./bs_${BOARD_TS}_tests_bsim_ieee802154_basic_prj_802154_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=1 -RealEncryption=0 \
  -testid=ieee802154_device -rs=24 -argstest rx_count 2

Execute ./bs_${BOARD_TS}_tests_bsim_ieee802154_basic_prj_802154_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=2 -RealEncryption=0 \
  -testid=ieee802154_device -rs=25 -argstest rx_count 1

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} \
  -D=3 -sim_length=10e6 -defmodem=BLE_simple -channel=multiatt -argschannel -at=40 \
  -file=$INFUSE_BASE/tests/bsim/ieee802154/basic/multiatt_symmetric.txt $@

wait_for_background_jobs
