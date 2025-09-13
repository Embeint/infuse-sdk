/**
 * Copyright (c) 2025 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>

#include "bs_types.h"
#include "bs_tracing.h"
#include "bstests.h"

#include <zephyr/net_buf.h>
#include <zephyr/net/ieee802154_radio.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);
#include <net_private.h>

#define FRAME_CTRL_BYTES 0x41, 0xd8
#define PAN_ID_BYTES                                                                               \
	(CONFIG_NET_CONFIG_IEEE802154_PAN_ID & 0xFF), (CONFIG_NET_CONFIG_IEEE802154_PAN_ID >> 8)
#define DEST_BROADCAST_BYTES 0xff, 0xff

#define FAIL(...)                                                                                  \
	do {                                                                                       \
		bst_result = Failed;                                                               \
		bs_trace_error_time_line(__VA_ARGS__);                                             \
	} while (0)

#define PASS(...)                                                                                  \
	do {                                                                                       \
		bst_result = Passed;                                                               \
		bs_trace_info_time(1, "PASSED: " __VA_ARGS__);                                     \
	} while (0)

#define WAIT_SECONDS 30                            /* seconds */
#define WAIT_TIME    (WAIT_SECONDS * USEC_PER_SEC) /* microseconds*/

extern enum bst_result_t bst_result;

struct test_cfg {
	uint16_t pan_id;
	int16_t expected_cnt;
	uint8_t channel;
	uint8_t ieee_addr[8];
	uint8_t recv_cnt;
	uint8_t sequence;
	bool do_tx;
};

static K_SEM_DEFINE(load_complete, 0, 1);

/* ieee802.15.4 device */
static struct ieee802154_radio_api *radio_api;
static const struct device *const ieee802154_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154));

struct test_cfg test_cfg = {
	.pan_id = CONFIG_NET_CONFIG_IEEE802154_PAN_ID,
	.expected_cnt = -1,
	.channel = CONFIG_NET_CONFIG_IEEE802154_CHANNEL,
	.ieee_addr = {0},
	.recv_cnt = 0,
	.do_tx = true,
};

/**
 * @brief Send a packet function
 *
 * @return int
 */
static int send(struct test_cfg *cfg)
{
	uint8_t mhr[] = {FRAME_CTRL_BYTES, cfg->sequence++, PAN_ID_BYTES, DEST_BROADCAST_BYTES};
	const uint8_t data[] = {0xda, 0x7a};
	int rc;
	struct net_buf *buf;
	struct net_pkt *pkt = net_pkt_rx_alloc_with_buffer(NULL, 256, AF_UNSPEC, 0, K_NO_WAIT);

	if (!pkt) {
		LOG_ERR("No more buffers");
		return -ENOMEM;
	}

	buf = net_buf_frag_last(pkt->buffer);
	if (!net_buf_tailroom(buf)) {
		LOG_ERR("No more buf space: buf %p len %u", buf, buf->len);

		rc = -ENOENT;
		goto end;
	}

	/* Data payload, 0xabcd pan ID, destination bradcast broadcast */
	net_buf_add_mem(buf, mhr, sizeof(mhr));
	net_buf_add_mem(buf, cfg->ieee_addr, sizeof(cfg->ieee_addr));
	net_buf_add_mem(buf, data, sizeof(data));

	/* Wait a short period for channel clear */
	for (uint8_t i = 0; i < 10; i++) {
		rc = radio_api->cca(ieee802154_dev);
		if (rc == 0) {
			break;
		}
	}

	if (rc < 0) {
		LOG_ERR("CCA failed %d", rc);
		goto end;
	}

	/* Send */
	rc = radio_api->tx(ieee802154_dev, IEEE802154_TX_MODE_CCA, pkt, buf);
	if (rc < 0) {
		LOG_ERR("Error transmit data %d", rc);
		goto end;
	}
end:
	net_pkt_unref(pkt);
	return rc;
}

/**
 * @brief Generate a random ieee 802.15.4 MAC address
 *
 */
static void generate_mac(struct test_cfg *cfg)
{
	uint8_t *mac = cfg->ieee_addr;

	mac[7] = 0x00;
	mac[6] = 0x12;
	mac[5] = 0x4b;
	mac[4] = 0x00;

	sys_rand_get(mac, 4U);

	mac[0] = (mac[0] & ~0x01) | 0x02;
	LOG_INF("MAC %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3],
		mac[4], mac[5], mac[6], mac[7]);
}

static int init_ieee802154(struct test_cfg *cfg)
{
	int rc;

	LOG_INF("Initialize ieee802.15.4");
	if (!device_is_ready(ieee802154_dev)) {
		LOG_ERR("IEEE 802.15.4 device not ready");
		return -ENODEV;
	}

	radio_api = (struct ieee802154_radio_api *)ieee802154_dev->api;

	/**
	 * Do actual initialization of the chip
	 */
	generate_mac(cfg);

	if (IEEE802154_HW_FILTER & radio_api->get_capabilities(ieee802154_dev)) {
		struct ieee802154_filter filter;

		/* Set ieee address */
		filter.ieee_addr = cfg->ieee_addr;
		rc = radio_api->filter(ieee802154_dev, true, IEEE802154_FILTER_TYPE_IEEE_ADDR,
				       &filter);
		if (rc < 0) {
			LOG_ERR("Error setting ieee address (%d)", rc);
			return rc;
		}

#ifdef CONFIG_NET_CONFIG_SETTINGS
		LOG_INF("Set panid %x", CONFIG_NET_CONFIG_IEEE802154_PAN_ID);
		filter.pan_id = CONFIG_NET_CONFIG_IEEE802154_PAN_ID;
		rc = radio_api->filter(ieee802154_dev, true, IEEE802154_FILTER_TYPE_PAN_ID,
				       &filter);
		if (rc < 0) {
			LOG_ERR("Error setting panid (%d)", rc);
			return rc;
		}
#endif /* CONFIG_NET_CONFIG_SETTINGS */
	}

#ifdef CONFIG_NET_CONFIG_SETTINGS
	LOG_INF("Set channel %u", CONFIG_NET_CONFIG_IEEE802154_CHANNEL);
	rc = radio_api->set_channel(ieee802154_dev, CONFIG_NET_CONFIG_IEEE802154_CHANNEL);
	if (rc < 0) {
		LOG_ERR("Error setting channel (%d)", rc);
		return rc;
	}
#endif /* CONFIG_NET_CONFIG_SETTINGS */

	/* Start ieee802154 */
	rc = radio_api->start(ieee802154_dev);
	if (rc < 0) {
		LOG_ERR("Error starting ieee802154 (%d)", rc);
		return rc;
	}

	return rc;
}

/**
 * @brief RX data callback
 *
 * @param iface interface
 * @param pkt pointer to the net packet
 * @return * int
 */
int net_recv_data(struct net_if *iface, struct net_pkt *pkt)
{
	struct net_buf *buf;

	LOG_INF("Received pkt %p, len %d", pkt, net_pkt_get_len(pkt));
	buf = net_buf_frag_last(pkt->buffer);
	LOG_HEXDUMP_DBG(buf->data, buf->len, "Payload:");
	test_cfg.recv_cnt++;

	return 0;
}

enum net_verdict ieee802154_handle_ack(struct net_if *iface, struct net_pkt *pkt)
{
	return NET_CONTINUE;
}

static void main_ieee802154_basic(void)
{
	struct test_cfg *cfg = &test_cfg;
	int rc;
	uint8_t delay;

	LOG_INF("Starting epacket 802154 application");

	/* Initialise net_pkt */
	net_pkt_init();

	/* Initialise ieee802154 device */
	rc = init_ieee802154(cfg);
	if (rc < 0) {
		FAIL("Unable to initialise ieee802154\n");
		return;
	}

	/* Random delay, send and wait for remaining duration. */
	sys_rand_get(&delay, 1);
	k_sleep(K_MSEC(delay));
	if (cfg->do_tx) {
		send(cfg);
	}
	k_sleep(K_MSEC(1000 - delay));

	/* Validate correct number of packets were observed */
	if (cfg->expected_cnt < 0) {
		/* There isn't an expected packet count. */
		PASS("%d Packets were received\n", cfg->recv_cnt);
		return;
	}

	if (cfg->recv_cnt == cfg->expected_cnt) {
		PASS("%d packets were received (as expected)\n", cfg->recv_cnt);
	} else {
		FAIL("%d packets were received, expected %d\n", cfg->recv_cnt, cfg->expected_cnt);
	}
}

/**
 * @brief Parse test arguments from -argstest
 *
 * Note: LOG/printk does not work in here
 * Note: args start at argv[0], not argv[1]
 *
 * @param argc
 * @param argv
 */
static void test_args(int argc, char *argv[])
{
	for (int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "rx_count") == 0) {
			test_cfg.expected_cnt = strtol(argv[++i], NULL, 10);
		} else if (strcmp(argv[i], "no_tx") == 0) {
			test_cfg.do_tx = false;
		}
	}
}

void test_tick(bs_time_t HW_device_time)
{
	if (bst_result != Passed) {
		FAIL("test failed (not passed after %i seconds)\n", WAIT_SECONDS);
	}
}

void test_init(void)
{
	bst_ticker_set_next_tick_absolute(WAIT_TIME);
	bst_result = In_progress;
}

static const struct bst_test_instance ieee802154_basic[] = {
	{
		.test_id = "ieee802154_device",
		.test_descr = "Basic Infuse-IoT ieee802154 device",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_ieee802154_basic,
		.test_args_f = test_args,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_ieee802154_basic(struct bst_test_list *tests)
{
	return bst_add_tests(tests, ieee802154_basic);
}

bst_test_install_t test_installers[] = {test_ieee802154_basic, NULL};

int main(void)
{
	bst_main();
	return 0;
}
