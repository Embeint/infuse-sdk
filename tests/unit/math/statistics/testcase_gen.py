#!/usr/bin/env python3

import random

import numpy


def test_output(a):
    means = [numpy.mean(a[:n]) for n in range(1, len(a) + 1)]
    variances = [numpy.var(a[:n], ddof=1) for n in range(1, len(a) + 1)]
    print(f"const int32_t array_values[] = {{ {', '.join([str(i) for i in a])} }};")
    print(f"const float array_means[] = {{ {', '.join([f'{i:.3f}f' for i in means])} }};")
    print(f"const float array_vars[] = {{ {', '.join([f'{i:.3f}f' for i in variances])} }};")


# To regenerate new outputs
array = [random.randint(-10, 30) for _ in range(20)]
# Values tested in C code
array = [
    18,
    12,
    24,
    7,
    3,
    20,
    -2,
    4,
    30,
    14,
    11,
    9,
    -2,
    11,
    27,
    7,
    16,
    13,
    5,
    3,
]
test_output(array)

# To regenerate new outputs
array = [random.randint(42000, 45000) for _ in range(20)]
# Values tested in C code
array = [
    43918,
    43770,
    44329,
    43522,
    44038,
    42123,
    42224,
    42704,
    42191,
    43489,
    42718,
    43157,
    44026,
    42036,
    42772,
    43420,
    43869,
    43368,
    43122,
    43051,
]
test_output(array)

# To regenerate new outputs
array = [random.randint(-999000, -998000) for _ in range(20)]
# Values tested in C code
array = [
    -998969,
    -998578,
    -998251,
    -998799,
    -999000,
    -998925,
    -998449,
    -998525,
    -998458,
    -998726,
    -998368,
    -998399,
    -998450,
    -998345,
    -998216,
    -998093,
    -998802,
    -998385,
    -998042,
    -998098,
]
test_output(array)
