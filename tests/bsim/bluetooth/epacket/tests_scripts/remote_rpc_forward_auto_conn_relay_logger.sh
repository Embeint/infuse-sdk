#!/usr/bin/env bash
# Copyright (c) 2026 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: Apache-2.0

# Originator -> Relay -> Device
# The originator forwards a data logger read through the relay. The device streams data packets
# sized to the maximum payload of its connection to the relay, which the relay limits so that
# every packet can be forwarded over its Bluetooth peripheral backhaul.

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

simulation_id="epacket_remote_rpc_forward_auto_conn_relay_logger"
verbosity_level=2

cd ${BSIM_OUT_PATH}/bin

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_device_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=0 -RealEncryption=0 \
  -testid=epacket_bt_relay_target -rs=23

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_gateway_prj_relay_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=1 -RealEncryption=0 \
  -testid=epacket_bt_gateway_relay_logger -rs=6

Execute ./bs_${BOARD_TS}_tests_bsim_bluetooth_epacket_gateway_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=2 -RealEncryption=0 \
  -testid=epacket_bt_gateway_relay_originator_logger -rs=48

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} \
  -D=3 -sim_length=14e6 $@

wait_for_background_jobs
