#!/usr/bin/env bash
# Copyright (c) 2026 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: Apache-2.0

# Originator -> Relay -> Device
# The originator requests the relay to connect to the device with the `bt_connect_infuse` RPC,
# as done by the Python gateway tools, then forwards a data logger read over that connection.
# The relay limits the MTU of the connection so that every packet streamed by the device can be
# forwarded over its Bluetooth peripheral backhaul.

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

simulation_id="epacket_remote_rpc_forward_relay_rpc_logger"
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
  -testid=epacket_bt_gateway_relay_originator_rpc_logger -rs=48

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} \
  -D=3 -sim_length=14e6 $@

wait_for_background_jobs
