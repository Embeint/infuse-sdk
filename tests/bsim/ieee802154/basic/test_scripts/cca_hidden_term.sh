#!/usr/bin/env bash
# Copyright (c) 2024 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: Apache-2.0

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

simulation_id="ieee802154_cca_hidden_term"
verbosity_level=2

cd ${BSIM_OUT_PATH}/bin

# Setup sim with four devices. Ensure that d2 cannot observe d0 and d3
# due to interference between the two. d1 and d3 transmit at the same time
# due to identical random seen and non-visibility invalidating cca.

Execute ./bs_${BOARD_TS}_tests_bsim_ieee802154_basic_prj_802154_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=0 -RealEncryption=0 \
  -testid=ieee802154_device -rs=22 -argstest no_tx

Execute ./bs_${BOARD_TS}_tests_bsim_ieee802154_basic_prj_802154_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=1 -RealEncryption=0 \
  -testid=ieee802154_device -rs=23 -argstest rx_count 0

Execute ./bs_${BOARD_TS}_tests_bsim_ieee802154_basic_prj_802154_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=2 -RealEncryption=0 \
  -testid=ieee802154_device -rs=25 -argstest rx_count 0 no_tx

Execute ./bs_${BOARD_TS}_tests_bsim_ieee802154_basic_prj_802154_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=3 -RealEncryption=0 \
  -testid=ieee802154_device -rs=23 -argstest rx_count 0

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} \
  -D=4 -sim_length=10e6 -defmodem=BLE_simple -channel=multiatt -argschannel -at=100 $@

wait_for_background_jobs
