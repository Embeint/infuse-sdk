# nRF Edge AI sample algorithm
Standalone sample algorithm: Identifies motion gestures.

## Build instructions
The algorithm does not use `west` to build.
Instead, generate all supported algorithm variants with CMake:

```sh
source /path/to/zephyr-sdk/setup.sh
export ZEPHYR_BASE=/path/to/zephyr
export ZEPHYR_SDK_INSTALL_DIR=/path/to/zephyr-sdk
cd infuse-sdk/samples/algorithm/gesture_edge_ai
rm -rf build  # Required when build was previously configured with host tools
cmake -S . -B build
cmake --build build
```

The stripped LLEXT files are written below the build directory, one variant
per CPU and floating-point ABI:

```text
build/<cpu>/<float-abi>/gesture_recognition.llext.stripped
```

For example:

```text
build/cortex_m3/soft/gesture_recognition.llext.stripped
build/cortex_m4/hard/gesture_recognition.llext.stripped
build/cortex_m33/softfp/gesture_recognition.llext.stripped
```
