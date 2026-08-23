/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_connectivity_impl.h>
#include <zephyr/net/dummy.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/ztest.h>

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/reboot.h>

LOG_MODULE_REGISTER(infuse_reboot_test, LOG_LEVEL_INF);

#define TEST_MAGIC      0x1290FEED
#define REBOOT_DELAY    K_SECONDS(2)
#define REBOOT_GRACE_MS MSEC_PER_SEC

struct test_state {
	uint32_t magic;
	int64_t schedule_uptime;
	int64_t disconnect_uptime;
	int64_t if_down_uptime;
	int64_t grace_marker_uptime;
	int disconnect_calls;
	int if_down_calls;
};

static __noinit struct test_state state;

static struct net_mgmt_event_callback iface_down_cb;
static struct k_work_delayable grace_marker_work;

static int test_netdev_init(const struct device *dev)
{
	return 0;
}

static void test_iface_init(struct net_if *iface)
{
	static uint8_t fake_lladdr[] = {0x00, 0x00, 0x5E, 0x00, 0x53, 0x01};

	net_if_set_link_addr(iface, fake_lladdr, sizeof(fake_lladdr), NET_LINK_DUMMY);
	net_if_flag_set(iface, NET_IF_NO_AUTO_START);
}

static int test_iface_send(const struct device *dev, struct net_pkt *pkt)
{
	return 0;
}

static struct dummy_api test_iface_api = {
	.iface_api.init = test_iface_init,
	.send = test_iface_send,
};

NET_DEVICE_INIT(test_net_iface, "test_net_iface", test_netdev_init, NULL, NULL, NULL,
		CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &test_iface_api, DUMMY_L2,
		NET_L2_GET_CTX_TYPE(DUMMY_L2), 127);

static void test_conn_api_init(struct conn_mgr_conn_binding *const binding)
{
	net_if_dormant_on(binding->iface);
}

static int test_conn_api_connect(struct conn_mgr_conn_binding *const binding)
{
	net_if_dormant_off(binding->iface);
	LOG_INF("sim backend connected at %lld ms", k_uptime_get());

	return 0;
}

static int test_conn_api_disconnect(struct conn_mgr_conn_binding *const binding)
{
	state.disconnect_calls++;
	state.disconnect_uptime = k_uptime_get();
	net_if_dormant_on(binding->iface);
	LOG_INF("sim backend disconnect requested at %lld ms (%lld ms after schedule)",
		state.disconnect_uptime, state.disconnect_uptime - state.schedule_uptime);

	return 0;
}

static struct conn_mgr_conn_api test_conn_api = {
	.init = test_conn_api_init,
	.connect = test_conn_api_connect,
	.disconnect = test_conn_api_disconnect,
};

struct test_conn_data {
};

#define TEST_CONN_IMPL_CTX_TYPE struct test_conn_data
CONN_MGR_CONN_DEFINE(TEST_CONN_IMPL, &test_conn_api);
CONN_MGR_BIND_CONN(test_net_iface, TEST_CONN_IMPL);

static void grace_marker(struct k_work *work)
{
	state.grace_marker_uptime = k_uptime_get();
	LOG_INF("grace marker ran at %lld ms (%lld ms after interface down)",
		state.grace_marker_uptime, state.grace_marker_uptime - state.if_down_uptime);
}

static void iface_down_handler(struct net_mgmt_event_callback *cb, uint64_t event,
			       struct net_if *iface)
{
	if (iface != NET_IF_GET(test_net_iface, 0)) {
		return;
	}

	state.if_down_calls++;
	state.if_down_uptime = k_uptime_get();
	LOG_INF("interface down event at %lld ms (%lld ms after schedule)", state.if_down_uptime,
		state.if_down_uptime - state.schedule_uptime);
	k_work_schedule(&grace_marker_work, K_MSEC(500));
}

ZTEST(infuse_reboot_hard, test_delayed_network_shutdown)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	struct infuse_reboot_state reboot_state;
	struct net_if *iface = NET_IF_GET(test_net_iface, 0);
	ssize_t rc;

	rc = KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	zassert_equal(sizeof(reboots), rc);

	switch (reboots.count) {
	case 1:
		LOG_INF("boot 1: preparing simulated network backend");
		state = (struct test_state){
			.magic = TEST_MAGIC,
			.disconnect_uptime = -1,
			.if_down_uptime = -1,
			.grace_marker_uptime = -1,
		};

		k_work_init_delayable(&grace_marker_work, grace_marker);
		net_mgmt_init_event_callback(&iface_down_cb, iface_down_handler, NET_EVENT_IF_DOWN);
		net_mgmt_add_event_callback(&iface_down_cb);

		zassert_true(conn_mgr_if_is_bound(iface));
		zassert_ok(conn_mgr_if_set_flag(iface, CONN_MGR_IF_NO_AUTO_CONNECT, true));
		zassert_ok(net_if_up(iface));
		zassert_ok(conn_mgr_if_connect(iface));
		zassert_true(net_if_is_admin_up(iface));
		zassert_false(net_if_is_dormant(iface));

		state.schedule_uptime = k_uptime_get();
		LOG_INF("scheduling delayed reboot at %lld ms with %lld ms delay",
			state.schedule_uptime, k_ticks_to_ms_floor64(REBOOT_DELAY.ticks));
		infuse_reboot_schedule(INFUSE_REBOOT_RPC, 0x1234, 0x5678, REBOOT_DELAY);

		k_sleep(K_SECONDS(1));
		LOG_INF("1 second after scheduling: admin_up=%d dormant=%d disconnect_calls=%d "
			"if_down_calls=%d",
			net_if_is_admin_up(iface), net_if_is_dormant(iface), state.disconnect_calls,
			state.if_down_calls);
		zassert_true(net_if_is_admin_up(iface));
		zassert_false(net_if_is_dormant(iface));
		zassert_equal(0, state.disconnect_calls);
		zassert_equal(0, state.if_down_calls);

		k_sleep(K_SECONDS(3));
		zassert_unreachable("Device did not reboot");
		break;
	case 2:
		LOG_INF("boot 2: validating timings after reboot");
		zassert_equal(TEST_MAGIC, state.magic);
		zassert_equal(1, state.disconnect_calls);
		zassert_equal(1, state.if_down_calls);
		LOG_INF("timing summary: disconnect=%lld ms after schedule, if_down=%lld ms "
			"after schedule, grace_marker=%lld ms after if_down",
			state.disconnect_uptime - state.schedule_uptime,
			state.if_down_uptime - state.schedule_uptime,
			state.grace_marker_uptime - state.if_down_uptime);
		zassert_within(state.disconnect_uptime - state.schedule_uptime,
			       k_ticks_to_ms_floor64(REBOOT_DELAY.ticks), 100);
		zassert_within(state.if_down_uptime, state.disconnect_uptime, 100);
		zassert_within(state.grace_marker_uptime - state.if_down_uptime, 500, 100);

		rc = infuse_reboot_state_query(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal(INFUSE_REBOOT_RPC, reboot_state.reason);
		zassert_equal(0x1234, reboot_state.info.generic.info1);
		zassert_equal(0x5678, reboot_state.info.generic.info2);
		LOG_INF("reboot metadata uptime=%u seconds", reboot_state.uptime);
		zassert_true(reboot_state.uptime >= 3);
		zassert_true(reboot_state.uptime < 4);
		break;
	default:
		zassert_unreachable("Unexpected reboot count");
		break;
	}
}

void *test_init(void)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboot_fallback = {0}, reboot = {0};
	int rc;

	rc = kv_store_read_fallback(KV_KEY_REBOOTS, &reboot, sizeof(reboot), &reboot_fallback,
				    sizeof(reboot_fallback));
	if (rc == sizeof(reboot)) {
		reboot.count += 1;
		(void)KV_STORE_WRITE(KV_KEY_REBOOTS, &reboot);
	}
	return NULL;
}

ZTEST_SUITE(infuse_reboot_hard, NULL, test_init, NULL, NULL, NULL);
