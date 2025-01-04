#!/usr/bin/env bash
#
# Validate cPatch upgrade functionality on a local serial device

# Validate argument exists
if [ $# -ne 1 ]; then
    echo "No release configuration file supplied"
    exit 1
fi
if [ ! -f $1 ]; then
    echo "'$1' is not a file"
    exit 1
fi

# Serial application paths
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
APP_DIR=$SCRIPT_DIR/serial
APP_VER=$APP_DIR/VERSION

# Build original application release
echo "Building original application release"
west release-build -r $1 --skip-git

# Increment the major version number
sed -i -E 's/^(VERSION_MAJOR\s*=\s*)([0-9]+)/echo "\1$((\2 + 1))"/ge' "$APP_VER"

# Build new application release
echo "Building updated application release"
west release-build -r $1 --skip-git

# Get the output directories
first_release=$(ls -1d release-sample-* | head -n 1)
second_release=$(ls -1d release-sample-* | tail -n 1)

# Generate the patch file to go from the first to the second
echo "Generating patch file for upgrade"
west release-diff $first_release $second_release

# Flash the first release to the board and wait for boot
echo "Flashing original application release"
west release-flash -r $first_release
echo "Waiting for application to boot..."
sleep 15.0

# Display the current application version
infuse rpc --gateway application_info

# Perform the patch to the updated version
patch_file=$(ls -1 $second_release/diffs/* | head -n 1)
echo "Upgrading from $first_release to $second_release"
infuse rpc --gateway file_write_basic --cpatch -f $patch_file
