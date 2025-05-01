#!/usr/bin/env sh

echo "BabbleSim Coverage"

export ZEPHYR_BASE=`pwd`/zephyr
export INFUSE_BASE=`pwd`/infuse-sdk
export WORK_DIR=`pwd`/bsim_out
# Run BabbleSim tests
$INFUSE_BASE/tests/bsim/ci.bt.sh
$INFUSE_BASE/tests/bsim/ci.802154.sh
$INFUSE_BASE/tests/bsim/ci.serial.sh

# Disable branch coverage on LOG_ and __ASSERT macros
BRANCH_LOG="(^\s*LOG_(?:HEXDUMP_)?(?:DBG|INF|WRN|ERR)\(.*)"
BRANCH_ASSERT="(^\s*__ASSERT(?:_EVAL|_NO_MSG|_POST_ACTION)?\(.*)"

# Generate coverage.json
gcovr -r $INFUSE_BASE \
    --gcov-ignore-parse-errors=negative_hits.warn_once_per_file \
    --gcov-executable gcov -e tests/* -e .*generated.* -e .*/tests/.* -e .*/samples/.* \
    --exclude-branches-by-pattern "$BRANCH_LOG|$BRANCH_ASSERT" \
    --merge-mode-functions=separate --json \
    -o $WORK_DIR/coverage.json \
    $WORK_DIR

# Generate html
mkdir -p $WORK_DIR/html
gcovr -r $INFUSE_BASE --html $WORK_DIR/html/index.html --html-details \
    --add-tracefile $WORK_DIR/coverage.json
