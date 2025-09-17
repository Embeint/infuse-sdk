#!/usr/bin/env sh

echo "Twister Coverage - mps2/an385"
./zephyr/scripts/twister -i --force-color -N -v --filter runnable \
    -p mps2/an385 --coverage -T infuse-sdk/tests --coverage-tool gcovr \
    -x=CONFIG_TEST_EXTRA_STACK_SIZE=4096 -e nano --coverage-basedir infuse-sdk
