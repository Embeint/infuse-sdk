.. _platform-design-goals:

Design Goals
############

The Infuse-IoT platform is designed in alignment with the following goals.

Machine learning support
************************

Sending all data that a device generates to the cloud is typically undesirable
due to power, bandwidth and cloud-compute cost limitations. By enabling machine
learning algorithms to be dynamically loaded into a base application,
higher-level context information can be extracted from the raw data streams and
sent directly, saving both cost and energy.

.. _platform-design-goals-non-ip:

Non-IP connectivity
*******************

Solutions limited by size, weight or cost constraints may choose to develop
hardware without built-in Internet-Protocol support. The Infuse platform
enables these solutions through multi-hop communications across short range
radios, primarily Bluetooth Low Energy.

.. _platform-design-goals-intermittent-comms:

Intermittent connectivity
*************************

Connectivity through your preferred backhaul interface is not always guaranteed
to exist, but that doesn't mean that your devices should stop working. Infuse
is designed to gracefully handle intermittent connectivity situations, whether
networking is lost for an hour or a year.

Ultra low-power operation
*************************

The Infuse platform is focused on ultra low-power devices. By reaching down
into the application layer, Infuse can help ensure that your hardware lasts as
long as possible between recharges or battery swaps. By providing rich
abstractions that handle the most common IoT use cases we minimise the
development effort required. And because these abstractions have been
battle-tested across multiple applications, you can be confident in software
stability from day one.


.. _platform-design-goals-d2d-comms:

Device-to-Device communications
*******************************

Communicating with other local Infuse devices enables another level of data
collection and context awareness. Direct links over Bluetooth GATT can enable
a higher power device to record data from a more limited peer, or uplink data
through an IP-enabled neighbour. Context awareness from nearby devices
broadcasting can enable devices without IP connectivity to determine their
own location without GNSS, greater temporal resolution in location data,
and more.

Hardware Agnostic
*****************

The Infuse-IoT platform is intended to be reasonably agnostic to the choice
of microcontroller and sensors on the board. Infuse leverages the Zephyr RTOS
project to provide the required abstractions that makes this possible.

Observability
*************

Debugging embedded devices can be a challenging endevour. Infuse-IoT aims to
provide as much visibility as possible into both the embedded SDK code and
the flow of data through the cloud services to simplify this process and
monitor standard operations.

Scalable
********

The Infuse platform is designed to scale from 10 to 10 million devices.

Secure by design
****************

The Infuse platform is committed to enabling organisations to meet security
standards such as `ETSI EN 303 645`_ and `NIST IR 8425`_. By integrating
industry standard encryption protocols over all communications, we help secure
your IoT deployments without compromising on functionality.

.. _ETSI EN 303 645: https://www.etsi.org/technologies/consumer-iot-security
.. _NIST IR 8425: https://csrc.nist.gov/pubs/ir/8425/final
