#!/usr/bin/env sh

APP_DIR=infuse-sdk/samples/releases/serial
APP_VER=$APP_DIR/VERSION

# Build original application release
echo "\nBuilding original application release"
west release-build -r infuse-sdk/samples/releases/serial-$1.yaml  --skip-git

# Increment the major version number
sed -i -E 's/^(VERSION_MAJOR\s*=\s*)([0-9]+)/echo "\1$((\2 + 1))"/ge' "$APP_VER"

# Build new application release
echo "\nBuilding updated application release"
west release-build -r infuse-sdk/samples/releases/serial-$1.yaml  --skip-git

# Get the output directories
first_release=$(ls -1d release-sample-serial-* | head -n 1)
second_release=$(ls -1d release-sample-serial-* | tail -n 1)

# Generate the patch file to go from the first to the second
echo "\nGenerating patch file for upgrade"
west release-diff $first_release $second_release

# Flash the first release to the board and wait for boot
echo "\nFlashing original application release"
west release-flash -r $first_release --erase
sleep 15.0

# Display the current application version
infuse rpc application_info

# Perform the patch to the updated version
patch_file=$(ls -1 $second_release/diffs/* | head -n 1)
echo "\nUpgrading from $first_release to $second_release"
infuse rpc file_write_basic --cpatch -f $patch_file
