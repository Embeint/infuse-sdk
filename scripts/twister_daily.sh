#!/usr/bin/env sh
VENDORS='--vendor nordic --vendor zephyr --vendor embeint'

./zephyr/scripts/twister -T infuse-sdk/ $VENDORS -M --build-only --show-footprint
