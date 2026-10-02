# Algorithm Loader sample (using IMU gestures)

## Firmware Sample
This sample demonstrates running algorithms which can either be:
* A reference algorithm compiled as a built-in part of the firmware.
* A reference algorithm compiled as an LLEXT extension along with the firmware.
* A reference (or arbitrary) algorithms compiled separately and deployed to the device dynamically.

By default this firmware does not include any bundled algorithms, but they can be
enabled using Kconfig. This also makes it easy to test algorithms.

This firmware sample is a part of the
[Infuse-SDK Cloud Docs: Creating Embedded Algorithms Guide](https://docs.infuse-cloud.io/guides/embedded-iot/embedded-algorithms). Refer to it for more information and details.

### Default Configuration
The default configuration does not build any algorithms, but does include the algorithm runner and loader components.
This means that separately compiled algorithms can be deployed to the device running this firmware.

### Built-in Algorithms
This sample can be configured to bundle an algorithm as part of the built-in firmware by setting:
```
CONFIG_BUNDLE_BUILTIN_ALGORITHM=y
```

The build system will include the standalone [imu_gestures](../../packager/nrf_edge_ai/imu_gestures) algorithm sample.
This will include the algorithm as part of a normal build process.
Doing so is a good way to test algorihtms and the build process without any LLEXT or
dynamic loading complexities.

### Bundled LLEXT Algorithms
This sample can also be configured to build an algorithm as a packaged LLEXT extension,
and bundle it as part of the firmware (as a binary blob). This can be done by setting:
```
CONFIG_BUNDLE_PACKAGED_ALGORITHM=y
```

The build system will build the [imu_gestures](../../packager/nrf_edge_ai/imu_gestures)
algorithm as a standalone binary, and include the binary blob with the firmware binary, and load it at runtime.
Doing so is a good way to test algorithms as an LLEXT extension without running into any dynamic loading complexities.

### Standalone Packaged Algorithms
By default, the firmware will allow packaged algorithms to be deployed dynamically.

This firmware can use the [imu_gestures](../../packager/samples/nrf_edge_ai/imu_gestures) and the
[upside_down](../../packager/samples/upside_down) algorithm samples.

Follow the respective [readme](../../packager/samples/upside_down/README.md) for building instructions,
and use the `filesystem_write` RPC to send the algorithm(s) to the device.

Refer to the [Infuse Cloud Docs: Deploying Models](https://docs.infuse-cloud.io/guides/embedded-iot/embedded-algorithms/deploy)
sub-guide for more details.

### Swapping out and testing your own model
The [imu_gestures](../../packager/nrf_edge_ai/imu_gestures) algorithm sample can act as a useful testbed.
When using an nRF Edge AI model, the generated components can be easily substituted to test your own model.
Refer to the
[Infuse Cloud Docs: Deploying Models](https://docs.infuse-cloud.io/guides/embedded-iot/embedded-algorithms/deploy)
sub-guide for more detail.
