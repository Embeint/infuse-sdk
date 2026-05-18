#!/usr/bin/env sh

APP_DIR=infuse-sdk/samples/releases/serial
APP_VER=$APP_DIR/VERSION
BOOT_DELAY=15

# Validate arguments
if [ "$#" -ne 1 ] && [ "$#" -ne 2 ]; then
    echo "Unexpected argument count"
    echo "Usage: cpatch_demo.sh board [commit]"
    exit 1
fi
RELEASE_FILE=infuse-sdk/samples/releases/serial-$1.yaml
if [ ! -f $RELEASE_FILE ]; then
    echo "$RELEASE_FILE does not exist!"
    exit 1
fi

if [ "$#" -eq 2 ]; then
    # Store current commit, checkout target commit
    START_COMMIT=`git -C $APP_DIR rev-parse HEAD`
    echo "Original commit: $START_COMMIT"
    echo "Checking out: $2"
    git -C $APP_DIR checkout $2 &> /dev/null
    west update > /dev/null
fi

# Build original application release
echo "\nBuilding original application release"
west release-build -r $RELEASE_FILE --skip-git

if [ "$#" -eq 2 ]; then
    # Revert to original commit
    START_COMMIT=`git -C $APP_DIR rev-parse HEAD`
    echo "Checking out: $START_COMMIT"
    git -C $APP_DIR checkout $START_COMMIT &> /dev/null
    west update > /dev/null
fi

# Increment the major version number
sed -i -E 's/^(VERSION_MAJOR\s*=\s*)([0-9]+)/echo "\1$((\2 + 1))"/ge' "$APP_VER"

# Build new application release
echo "\nBuilding updated application release"
west release-build -r $RELEASE_FILE --skip-git

# Get the output directories
first_release=$(ls -1d release-sample-serial-* | head -n 1)
second_release=$(ls -1d release-sample-serial-* | tail -n 1)

# Generate the patch file to go from the first to the second
echo "\nGenerating patch file for upgrade"
west release-diff -i $first_release -o $second_release

# Flash the first release to the board and wait for boot
echo "\nFlashing original application release"
west release-flash -r $first_release --erase
echo "\nGiving application $BOOT_DELAY seconds to boot"
sleep $BOOT_DELAY

# Display the current application version
infuse rpc --gateway application_info

# Perform the patch to the updated version
patch_file=$(ls -1 $second_release/diffs/* | head -n 1)
echo "\nUpgrading from $first_release to $second_release"
infuse rpc --gateway file_write_basic --cpatch --file $patch_file
