/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_offload.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/device.h>
#include <zephyr/net/conn_mgr/connectivity_wifi_mgmt.h>

struct wifi_sim_iface_data {
	struct k_work power_up;
	struct k_work power_down;
	struct k_work_delayable connect_success;
	struct k_work_delayable connect_failure;
	struct k_work disconnect;
	struct net_if *iface;
	bool ap_in_range;
	bool connecting;
	bool connected;
};

LOG_MODULE_REGISTER(sim_wifi, LOG_LEVEL_INF);

static int offload_dummy_get(sa_family_t family, enum net_sock_type type,
			     enum net_ip_protocol ip_proto, struct net_context **context)
{
	return -1;
}

/* Placeholders, until Zephyr IP stack updated to handle a NULL net_offload */
static struct net_offload offload_dummy = {
	.get = offload_dummy_get,
	.bind = NULL,
	.listen = NULL,
	.connect = NULL,
	.accept = NULL,
	.send = NULL,
	.sendto = NULL,
	.recv = NULL,
	.put = NULL,
};

static void sim_wifi_connect_success_work(struct k_work *work)
{
	struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	struct wifi_sim_iface_data *data =
		CONTAINER_OF(delayable, struct wifi_sim_iface_data, connect_success);

	LOG_INF("Submitting connection success");
	data->connected = true;
	data->connecting = false;
	net_if_dormant_off(data->iface);
	wifi_mgmt_raise_connect_result_event(data->iface, 0);
}

static void sim_wifi_connect_failure_work(struct k_work *work)
{
	struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	struct wifi_sim_iface_data *data =
		CONTAINER_OF(delayable, struct wifi_sim_iface_data, connect_failure);

	LOG_INF("Submitting connection failed");
	data->connected = false;
	data->connecting = false;
	wifi_mgmt_raise_connect_result_event(data->iface, -ETIMEDOUT);
}

static int sim_wifi_connect(const struct device *dev, struct wifi_connect_req_params *params)
{
	struct wifi_sim_iface_data *data = dev->data;

	if (data->connecting || data->connected) {
		return -EINVAL;
	}

	if (!data->ap_in_range) {
		LOG_INF("Connection will fail (%s)", "out of range");
		k_work_schedule(&data->connect_failure, K_MSEC(500));
	} else if (params->security != WIFI_SECURITY_TYPE_PSK) {
		LOG_INF("Connection will fail (%s)", "bad security");
		k_work_schedule(&data->connect_failure, K_MSEC(500));
	} else if ((params->ssid_length != strlen(CONFIG_WIFI_SIM_AP_SSID)) ||
		   (strncmp(params->ssid, CONFIG_WIFI_SIM_AP_SSID, params->ssid_length) != 0)) {
		LOG_INF("Connection will fail (%s)", "bad SSID");
		k_work_schedule(&data->connect_failure, K_MSEC(500));
	} else if ((params->psk_length != strlen(CONFIG_WIFI_SIM_AP_PSK)) ||
		   (strncmp(params->psk, CONFIG_WIFI_SIM_AP_PSK, params->psk_length) != 0)) {
		LOG_INF("Connection will fail (%s)", "bad PSK");
		k_work_schedule(&data->connect_failure, K_MSEC(500));
	} else {
		/* Checks passed, simulated connection */
		LOG_INF("Connection will succeed");
		k_work_schedule(&data->connect_success, K_MSEC(500));
	}
	data->connecting = true;
	return 0;
}

static void sim_wifi_disconnect_work(struct k_work *work)
{
	struct wifi_sim_iface_data *data =
		CONTAINER_OF(work, struct wifi_sim_iface_data, disconnect);

	data->connected = false;
	data->connecting = false;
	net_if_dormant_on(data->iface);
	wifi_mgmt_raise_disconnect_result_event(data->iface, 0);
}

static int sim_wifi_disconnect(const struct device *dev)
{
	struct wifi_sim_iface_data *data = dev->data;

	if (data->connecting) {
		LOG_INF("Triggering disconnect while connecting");
		k_work_cancel_delayable(&data->connect_success);
		k_work_reschedule(&data->connect_failure, K_NO_WAIT);
		k_sleep(K_TICKS(1));
	} else if (data->connected) {
		LOG_INF("Triggering disconnect when connected");
		k_work_submit(&data->disconnect);
		k_sleep(K_TICKS(1));
	} else {
		LOG_DBG("No connection present");
		return -EINVAL;
	}
	return 0;
}

static void sim_wifi_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct wifi_sim_iface_data *data = dev->data;

	iface->if_dev->offload = &offload_dummy;
	data->iface = iface;

	if (!IS_ENABLED(CONFIG_WIFI_SIM_IF_AUTO_START)) {
		net_if_flag_set(iface, NET_IF_NO_AUTO_START);
	}

	net_if_carrier_off(iface);
	net_if_dormant_on(iface);
}

static void sim_wifi_power_up_work(struct k_work *work)
{
	struct wifi_sim_iface_data *data = CONTAINER_OF(work, struct wifi_sim_iface_data, power_up);

	net_if_carrier_on(data->iface);
}

static void sim_wifi_power_down_work(struct k_work *work)
{
	struct wifi_sim_iface_data *data =
		CONTAINER_OF(work, struct wifi_sim_iface_data, power_down);

	net_if_carrier_off(data->iface);
	net_if_carrier_off(data->iface);
}

static int sim_wifi_enable(const struct net_if *iface, bool state)
{
	const struct device *dev = iface->if_dev->dev;
	struct wifi_sim_iface_data *data = dev->data;

	if (state) {
		k_work_submit(&data->power_up);
	} else {
		k_work_submit(&data->power_down);
	}
	return 0;
}

static int sim_wifi_dev_init(const struct device *dev)
{
	struct wifi_sim_iface_data *data = dev->data;

	data->ap_in_range = true;
	k_work_init(&data->power_up, sim_wifi_power_up_work);
	k_work_init(&data->power_down, sim_wifi_power_down_work);
	k_work_init_delayable(&data->connect_success, sim_wifi_connect_success_work);
	k_work_init_delayable(&data->connect_failure, sim_wifi_connect_failure_work);
	k_work_init(&data->disconnect, sim_wifi_disconnect_work);

	return 0;
}

static enum offloaded_net_if_types sim_wifi_get_type(void)
{
	return L2_OFFLOADED_NET_IF_TYPE_WIFI;
}

static const struct wifi_mgmt_ops sim_wifi_mgmt = {
	.connect = sim_wifi_connect,
	.disconnect = sim_wifi_disconnect,
};

static const struct net_wifi_mgmt_offload sim_wifi_api = {
	.wifi_iface.iface_api.init = sim_wifi_init,
	.wifi_iface.get_type = sim_wifi_get_type,
	.wifi_iface.enable = sim_wifi_enable,
	.wifi_mgmt_api = &sim_wifi_mgmt,
};

static struct wifi_sim_iface_data data;
NET_DEVICE_OFFLOAD_INIT(sim_wifi_dev, "sim_wifi_dev", sim_wifi_dev_init, NULL, &data, NULL,
			CONFIG_WIFI_INIT_PRIORITY, &sim_wifi_api, NET_ETH_MTU);
CONNECTIVITY_WIFI_MGMT_BIND(sim_wifi_dev);

void wifi_sim_in_network_range(bool in_range)
{
	const struct device *dev = &DEVICE_NAME_GET(sim_wifi_dev);
	struct wifi_sim_iface_data *data = dev->data;

	LOG_INF("AP is now %s", in_range ? "in range" : "out of range");
	data->ap_in_range = in_range;
}

void wifi_sim_trigger_disconnect(void)
{
	const struct device *dev = &DEVICE_NAME_GET(sim_wifi_dev);
	struct wifi_sim_iface_data *data = dev->data;

	if (data->connected) {
		LOG_INF("Simulating network disconnect");
		k_work_submit(&data->disconnect);
		k_sleep(K_TICKS(1));
	}
}
