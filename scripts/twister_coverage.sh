#!/usr/bin/env sh

echo "Twister Coverage - mps2_an385"
./zephyr/scripts/twister -i --force-color -N -v --filter runnable \
    -p mps2_an385 --coverage -T infuse-sdk/tests --coverage-tool gcovr \
    -xCONFIG_TEST_EXTRA_STACK_SIZE=8192 -e nano --coverage-basedir infuse-sdk
