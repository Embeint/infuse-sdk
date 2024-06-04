#!/usr/bin/env sh
COMMON_ARGS='--force-color --inline-logs -v -N -M --retry-failed 3'

echo "Twister - Unit Testing"
./zephyr/scripts/twister -T infuse-sdk/ $COMMON_ARGS --platform unit_testing
echo "Twister - Vendor"
./zephyr/scripts/twister -T infuse-sdk/ $COMMON_ARGS --vendor nordic --vendor zephyr --integration
