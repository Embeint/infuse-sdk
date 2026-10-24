#!/usr/bin/env sh

DIFF_SCRIPT="-m infuse_iot.cpatch"

HELLO=bin/hello_world.bin
PHILO=bin/philosophers.bin

python3 $DIFF_SCRIPT generate $HELLO $HELLO patch/hello_world-to-hello_world.bin
python3 $DIFF_SCRIPT generate $HELLO $PHILO patch/hello_world-to-philosophers.bin
python3 $DIFF_SCRIPT generate $PHILO $HELLO patch/philosophers-to-hello_world.bin
python3 $DIFF_SCRIPT generate $HELLO $PHILO patch/hello_world-to-philosophers-delta.bin \
    --encoder shortest-path-delta
python3 $DIFF_SCRIPT generate $PHILO $HELLO patch/philosophers-to-hello_world-delta.bin \
    --encoder shortest-path-delta
python3 $DIFF_SCRIPT validation $HELLO patch/hello_world-bad-len.bin --invalid-length
python3 $DIFF_SCRIPT validation $HELLO patch/hello_world-bad-crc.bin --invalid-crc
python3 $DIFF_SCRIPT validation $HELLO patch/hello_world-validate.bin
python3 $DIFF_SCRIPT validation $HELLO patch/hello_world-bad-len-delta.bin --invalid-length --delta
python3 $DIFF_SCRIPT validation $HELLO patch/hello_world-bad-crc-delta.bin --invalid-crc --delta
python3 $DIFF_SCRIPT validation $HELLO patch/hello_world-validate-delta.bin --delta
