#!/usr/bin/env python3
#
# SPDX-License-Identifier: FSL-1.1-ALv2

import math
import random


def random_int16():
    return random.randint(-(2**15), 2**15)


for _ in range(10):
    x = random_int16()
    y = random_int16()
    mag = math.sqrt((x * x) + (y * y))
    print(f"zassert_equal({int(mag)}, math_vector_xy_magnitude({x}, {y}));")

for _ in range(10):
    x = random_int16()
    y = random_int16()
    z = random_int16()
    mag = math.sqrt((x * x) + (y * y) + (z * z))
    print(f"zassert_equal({int(mag)}, math_vector_xyz_magnitude({x}, {y}, {z}));")
