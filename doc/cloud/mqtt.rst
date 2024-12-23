.. _infuse-cloud-mqtt:

MQTT Data Stream
################

The purpose of the Infuse-IoT MQTT data stream is to provide the real-time
source of data that has been forwarded to the cloud from your devices.

It is expected that each customer will have a MQTT consumer permanently
connected to the Infuse-IoT message broker in order to receive this data.

Authentication
**************

Data Queues
***********

Retention Policies
******************

Excluding device metadata that is observed for the purposes of enabling
the :ref:`infuse-cloud-rest-api` to function, data served through the MQTT
data queue is not retained on any Infuse-IoT servers once it has been delivered
from the data queue.
