#!/usr/bin/env python3
# Copyright (c) 2026 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: FSL-1.1-ALv2

"""Extract the pinned Pico combined firmware header for WHD's binary resources."""

import pathlib
import re
import sys

header = pathlib.Path(sys.argv[1]).read_text()
output = pathlib.Path(sys.argv[2])
array = re.search(r"=\s*\{([^}]+)\};", header)
if array is None:
    raise ValueError("Missing combined firmware array")
image = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", array[1]))
fw_len = int(re.search(r"#define CYW43_WIFI_FW_LEN \((\d+)\)", header)[1])
clm_len = int(re.search(r"#define CYW43_CLM_LEN \((\d+)\)", header)[1])
clm_offset = (fw_len + 511) // 512 * 512
if len(image) != clm_offset + clm_len or b"-btsdio " not in image[:fw_len]:
    raise ValueError("Invalid combined firmware or missing Bluetooth shared-bus support")
if image[clm_offset:clm_offset + 4] != b"BLOB":
    raise ValueError("Invalid CLM resource")
for name, data in (("wifi.bin", image[:fw_len]), ("wifi.clm_blob", image[clm_offset:])):
    path = output / name
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)
print(f"{fw_len};{clm_len}")
