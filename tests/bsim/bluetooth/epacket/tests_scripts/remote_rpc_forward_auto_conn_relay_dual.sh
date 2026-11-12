#!/usr/bin/env bash
# Copyright (c) 2026 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: Apache-2.0

# Originator A -> Relay -> Device 0
# Originator B -> Relay -> Device 1
# Two originators connect to the same relay at once, each sending INFUSE_EPACKET_FORWARD_AUTO_CONN
# packets for a different device. The relay uses the Bluetooth peripheral interface as its
# backhaul, so every response is returned to both originators.

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

simulation_id="epacket_remote_rpc_forward_auto_conn_relay_dual"
verbosity_level=2

cd ${BSIM_OUT_PATH}/bin

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_device_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=0 -RealEncryption=0 \
  -testid=epacket_bt_relay_target -rs=23

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_device_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=1 -RealEncryption=0 \
  -testid=epacket_bt_relay_target -rs=24

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_gateway_prj_relay_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=2 -RealEncryption=0 \
  -testid=epacket_bt_gateway_relay_dual -rs=6

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_gateway_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=3 -RealEncryption=0 \
  -testid=epacket_bt_gateway_relay_originator_a -rs=48

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_gateway_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=4 -RealEncryption=0 \
  -testid=epacket_bt_gateway_relay_originator_b -rs=49

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} \
  -D=5 -sim_length=14e6 $@

wait_for_background_jobs
