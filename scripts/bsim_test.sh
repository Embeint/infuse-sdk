#!/usr/bin/env sh

echo "BabbleSim Testing"

export INCR_BUILD=1
export ZEPHYR_BASE=`pwd`/zephyr
export INFUSE_BASE=`pwd`/infuse-sdk
export WORK_DIR=`pwd`/bsim_out

if [ $# -eq 0 ]; then
    # Run all BabbleSim tests
    $INFUSE_BASE/tests/bsim/ci.bt.sh
    $INFUSE_BASE/tests/bsim/ci.serial.sh
    $INFUSE_BASE/tests/bsim/ci.802154.sh
else
    # Build all binaries
    nice $INFUSE_BASE/tests/bsim/bluetooth/compile.sh
    nice $INFUSE_BASE/tests/bsim/ieee802154/compile.sh
    nice $INFUSE_BASE/tests/bsim/serial/compile.sh
    # Run the provided test script
    "$@"
fi
