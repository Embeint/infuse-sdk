/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/conn_mgr_connectivity.h>

#include <infuse/time/epoch.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>

static struct epacket_interface_cb epacket_cb;
static K_SEM_DEFINE(epacket_udp_ready, 0, 1);
static K_SEM_DEFINE(tx_complete, 0, 1);

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static void udp_interface_state(uint16_t current_max_payload, void *user_ctx)
{
	if (current_max_payload > 0) {
		k_sem_give(&epacket_udp_ready);
	}
}

static void last_packet_sent(const struct device *dev, struct net_buf *pkt, int result,
			     void *user_data)
{
	k_sem_give(&tx_complete);
}

int main(void)
{
	const struct device *udp = DEVICE_DT_GET(DT_NODELABEL(epacket_udp));
	uint32_t throughput, pkts_sent;
	struct net_buf *buf;
	uint16_t tailroom;
	uint64_t bytes_sent;
	int64_t t_start, t_end;

	/* Register for UDP notifications */
	epacket_cb.interface_state = udp_interface_state;
	epacket_register_callback(udp, &epacket_cb);

	/* Wait a few seconds to start the cycle */
	k_sleep(K_SECONDS(5));

	while (true) {
		/* Turn on the interface, wait for UDP to be ready */
		conn_mgr_all_if_up(true);
		k_sem_take(&epacket_udp_ready, K_FOREVER);

		/* Send the payload in largest chunks we can */
		pkts_sent = 0;
		bytes_sent = 0;
		LOG_INF("Starting send");
		t_start = k_uptime_get();
		while (bytes_sent < CONFIG_BULK_UPLOAD_BYTES) {
			buf = epacket_alloc_tx_for_interface(udp, K_FOREVER);
			epacket_set_tx_metadata(buf, EPACKET_AUTH_DEVICE, 0x00, 0xFF,
						EPACKET_ADDR_ALL);

			/* Add "payload" and update counters */
			tailroom = net_buf_tailroom(buf);
			net_buf_add(buf, tailroom);
			bytes_sent += tailroom;
			pkts_sent++;

			/* Attach callback on last packet */
			if (bytes_sent >= CONFIG_BULK_UPLOAD_BYTES) {
				epacket_set_tx_callback(buf, last_packet_sent, NULL);
			}

			/* Queue packet for transmission */
			epacket_queue(udp, buf);
		}
		/* Wait for transmissions to finish */
		k_sem_take(&tx_complete, K_FOREVER);
		/* Unfortunately zsock_send() returns before actually sending */
		k_sleep(K_MSEC(50));
		t_end = k_uptime_get();

		/* Power down interfaces */
		conn_mgr_all_if_down(false);

		/* Calculate throughput and print stats */
		throughput = bytes_sent * 8 * 1000 / (t_end - t_start) / 1024;
		LOG_INF("Sent %d packets in %lld ms (%d kbps)", pkts_sent, t_end - t_start,
			throughput);

		/* Wait for the next round */
		k_sleep(K_SECONDS(CONFIG_BULK_UPLOAD_PERIOD));
	}

	/* No work to do in main thread */
	k_sleep(K_FOREVER);
}
