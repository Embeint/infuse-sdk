/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/types.h>
#include <infuse/reboot.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/keys.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/epacket/interface/epacket_bt_adv.h>
#include <infuse/security.h>

#include "../subsys/epacket/interfaces/epacket_internal.h"

K_SEM_DEFINE(reboot_request, 0, 2);

void infuse_reboot(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2)
{
	k_sem_give(&reboot_request);
}

void infuse_reboot_delayed(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2,
			   k_timeout_t delay)
{
	k_sem_give(&reboot_request);
}

ZTEST(epacket_common, test_global_flags)
{
	uint16_t global_flags_all = EPACKET_FLAGS_CLOUD_FORWARDING | EPACKET_FLAGS_CLOUD_SELF;

	zassert_equal(0, epacket_global_flags_get(), "Bad initial state");

	/* Invalid flags */
	epacket_global_flags_set(~global_flags_all);
	zassert_equal(0, epacket_global_flags_get(), "Invalid flags not ignored");
	epacket_global_flags_set(global_flags_all);
	zassert_equal(global_flags_all, epacket_global_flags_get(), "Flags not set");
	epacket_global_flags_set(EPACKET_FLAGS_CLOUD_FORWARDING);
	zassert_equal(EPACKET_FLAGS_CLOUD_FORWARDING, epacket_global_flags_get(), "Flags not set");
	epacket_global_flags_set(EPACKET_FLAGS_CLOUD_SELF);
	zassert_equal(EPACKET_FLAGS_CLOUD_SELF, epacket_global_flags_get(), "Flags not set");
	epacket_global_flags_set(0);
	zassert_equal(0, epacket_global_flags_get(), "Flags not reset");
}

ZTEST(epacket_common, test_alloc_auto_flags)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_tx_metadata *tx_meta;
	struct net_buf *buf;

	buf = epacket_alloc_tx_for_interface(epacket_dummy, K_NO_WAIT);
	zassert_not_null(buf);
	tx_meta = net_buf_user_data(buf);

	epacket_set_tx_metadata(buf, EPACKET_AUTH_DEVICE, 0, INFUSE_TDF, EPACKET_ADDR_ALL);
	zassert_equal(0, tx_meta->flags, "Flags not empty");

	epacket_global_flags_set(EPACKET_FLAGS_CLOUD_FORWARDING);
	epacket_set_tx_metadata(buf, EPACKET_AUTH_DEVICE, 0, INFUSE_TDF, EPACKET_ADDR_ALL);
	zassert_equal(EPACKET_FLAGS_CLOUD_FORWARDING, tx_meta->flags, "Global flags not applied");

	epacket_global_flags_set(EPACKET_FLAGS_CLOUD_SELF);
	epacket_set_tx_metadata(buf, EPACKET_AUTH_DEVICE, 0, INFUSE_TDF, EPACKET_ADDR_ALL);
	zassert_equal(EPACKET_FLAGS_CLOUD_SELF, tx_meta->flags, "Global flags not applied");

	epacket_global_flags_set(0);

	net_buf_unref(buf);
}

ZTEST(epacket_common, test_encrypt_unknown_key)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	uint32_t default_network = infuse_security_network_key_identifier();
	uint8_t payload[6] = {0};
	struct net_buf *buf;
	int rc;

	buf = epacket_alloc_tx_for_interface(epacket_dummy, K_NO_WAIT);
	net_buf_reserve(buf, 32);
	zassert_not_null(buf);

	/* Arbitrary network key metadata and payload */
	epacket_set_tx_metadata(buf, EPACKET_AUTH_NETWORK, 0, INFUSE_TDF, EPACKET_ADDR_ALL);
	net_buf_add_mem(buf, payload, sizeof(payload));

	/* Network IDs we don't know can't be encrypted */
	rc = epacket_unversioned_v0_encrypt(buf, EPACKET_KEY_INTERFACE_BT_GATT,
					    default_network + 1);
	zassert_equal(-1, rc);
	rc = epacket_versioned_v0_encrypt(buf, EPACKET_KEY_INTERFACE_BT_GATT, default_network + 1);
	zassert_equal(-1, rc);

	net_buf_unref(buf);
}

ZTEST(epacket_common, test_alloc_failure)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct net_buf *tx_bufs[CONFIG_EPACKET_BUFFERS_TX];
	struct net_buf *rx_bufs[CONFIG_EPACKET_BUFFERS_RX];

	/* Allocate all TX buffers, then check failure */
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_TX; i++) {
		zassert_equal(CONFIG_EPACKET_BUFFERS_TX - i, epacket_num_buffers_free_tx());
		tx_bufs[i] = epacket_alloc_tx_for_interface(epacket_dummy, K_NO_WAIT);
		zassert_not_null(tx_bufs[i]);
	}
	zassert_equal(0, epacket_num_buffers_free_tx());
	zassert_is_null(epacket_alloc_tx_for_interface(epacket_dummy, K_NO_WAIT));

	/* Allocate all RX buffers, then check failure */
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_RX; i++) {
		zassert_equal(CONFIG_EPACKET_BUFFERS_RX - i, epacket_num_buffers_free_rx());
		rx_bufs[i] = epacket_alloc_rx(K_NO_WAIT);
		zassert_not_null(rx_bufs[i]);
	}
	zassert_equal(0, epacket_num_buffers_free_rx());
	zassert_is_null(epacket_alloc_rx(K_NO_WAIT));

	/* Free all buffers */
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_TX; i++) {
		net_buf_unref(tx_bufs[i]);
		zassert_equal(i + 1, epacket_num_buffers_free_tx());
	}
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_RX; i++) {
		net_buf_unref(rx_bufs[i]);
		zassert_equal(i + 1, epacket_num_buffers_free_rx());
	}
}

ZTEST(epacket_common, test_buffer_exhaustion)
{
	struct net_buf *tx_bufs[CONFIG_EPACKET_BUFFERS_TX];
	struct net_buf *rx_bufs[CONFIG_EPACKET_BUFFERS_RX];

	/** Allocate all buffers */
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_TX; i++) {
		tx_bufs[i] = epacket_alloc_tx(K_NO_WAIT);
		zassert_not_null(tx_bufs[i]);
	}
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_RX; i++) {
		rx_bufs[i] = epacket_alloc_rx(K_NO_WAIT);
		zassert_not_null(rx_bufs[i]);
	}

	/* Periodically release and reclaim a buffer */
	BUILD_ASSERT(4 > CONFIG_EPACKET_BUFFER_EXHAUSTION_TIMEOUT);
	for (int i = 0; i < 4; i++) {
		k_sleep(K_SECONDS(1));
		net_buf_unref(tx_bufs[0]);
		net_buf_unref(rx_bufs[0]);
		tx_bufs[0] = epacket_alloc_tx(K_NO_WAIT);
		rx_bufs[0] = epacket_alloc_rx(K_NO_WAIT);
		zassert_not_null(tx_bufs[0]);
		zassert_not_null(rx_bufs[0]);
	}
	/* Should not have rebooted */
	zassert_equal(-EBUSY, k_sem_take(&reboot_request, K_NO_WAIT));

	/* Sleep until both buffer watchdogs should have timed out */
	k_sleep(K_SECONDS(CONFIG_EPACKET_BUFFER_EXHAUSTION_TIMEOUT));
	zassert_equal(0, k_sem_take(&reboot_request, K_MSEC(10)));
	zassert_equal(0, k_sem_take(&reboot_request, K_MSEC(10)));

	/* Free all buffers */
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_TX; i++) {
		net_buf_unref(tx_bufs[i]);
	}
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_RX; i++) {
		net_buf_unref(rx_bufs[i]);
	}
}

ZTEST(epacket_common, test_buffer_exhaustion_none)
{
	struct net_buf *tx_bufs[CONFIG_EPACKET_BUFFERS_TX - 1];
	struct net_buf *rx_bufs[CONFIG_EPACKET_BUFFERS_RX - 1];

	/** Allocate all buffers but one */
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_TX - 1; i++) {
		tx_bufs[i] = epacket_alloc_tx(K_NO_WAIT);
		zassert_not_null(tx_bufs[i]);
	}
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_RX - 1; i++) {
		rx_bufs[i] = epacket_alloc_rx(K_NO_WAIT);
		zassert_not_null(rx_bufs[i]);
	}

	/* Should never reboot */
	zassert_equal(-EAGAIN, k_sem_take(&reboot_request, K_SECONDS(10)));

	/* Free all buffers */
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_TX - 1; i++) {
		net_buf_unref(tx_bufs[i]);
	}
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_RX - 1; i++) {
		net_buf_unref(rx_bufs[i]);
	}
}

ZTEST(epacket_common, test_alloc_not_connected)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct net_buf *tx_buf;

	/* Simulate interface not connected */
	epacket_dummy_set_max_packet(0);

	/* Packet is allocated */
	tx_buf = epacket_alloc_tx_for_interface(epacket_dummy, K_FOREVER);
	zassert_not_null(tx_buf);
	/* No payload space */
	zassert_equal(0, net_buf_tailroom(tx_buf));
	/* Free buffer */
	net_buf_unref(tx_buf);
}

ZTEST(epacket_common, test_receive)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));

	/* Working as expected */
	epacket_dummy_receive_api_override(true, 0);

	/* No work scheduled, requested to stop */
	zassert_false(epacket_dummy_receive_scheduled());
	zassert_equal(0, epacket_receive(epacket_dummy, K_NO_WAIT));
	zassert_false(epacket_dummy_receive_scheduled());

	/* No work scheduled, request for 1 second */
	zassert_equal(1, epacket_receive(epacket_dummy, K_SECONDS(1)));
	zassert_true(epacket_dummy_receive_scheduled());
	k_sleep(K_MSEC(950));
	zassert_true(epacket_dummy_receive_scheduled());
	k_sleep(K_MSEC(100));
	zassert_false(epacket_dummy_receive_scheduled());

	/* No work scheduled, request for 2 seconds then 1 second */
	zassert_equal(1, epacket_receive(epacket_dummy, K_SECONDS(2)));
	zassert_equal(1, epacket_receive(epacket_dummy, K_SECONDS(1)));
	k_sleep(K_MSEC(950));
	zassert_true(epacket_dummy_receive_scheduled());
	k_sleep(K_MSEC(100));
	zassert_false(epacket_dummy_receive_scheduled());

	/* No work scheduled, request for 1 second then 2 seconds */
	zassert_equal(1, epacket_receive(epacket_dummy, K_SECONDS(1)));
	zassert_equal(1, epacket_receive(epacket_dummy, K_SECONDS(2)));
	k_sleep(K_MSEC(1950));
	zassert_true(epacket_dummy_receive_scheduled());
	k_sleep(K_MSEC(100));
	zassert_false(epacket_dummy_receive_scheduled());

	/* No work scheduled, request forever */
	zassert_equal(0, epacket_receive(epacket_dummy, K_FOREVER));
	zassert_true(epacket_dummy_receive_scheduled());
	k_sleep(K_MSEC(2100));
	zassert_true(epacket_dummy_receive_scheduled());
	/* Cancel immediately */
	zassert_equal(0, epacket_receive(epacket_dummy, K_NO_WAIT));
	zassert_false(epacket_dummy_receive_scheduled());
}

ZTEST(epacket_common, test_receive_no_impl)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));

	epacket_dummy_receive_api_override(false, 0);

	zassert_equal(-ENOTSUP, epacket_receive(epacket_dummy, K_NO_WAIT));
	zassert_equal(-ENOTSUP, epacket_receive(epacket_dummy, K_FOREVER));
	zassert_equal(-ENOTSUP, epacket_receive(epacket_dummy, K_SECONDS(2)));

	epacket_dummy_receive_api_override(true, 0);
}

ZTEST(epacket_common, test_receive_error)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));

	epacket_dummy_receive_api_override(true, -EIO);
	zassert_false(epacket_dummy_receive_scheduled());

	/* Function call should fail to disable */
	zassert_equal(-EIO, epacket_receive(epacket_dummy, K_NO_WAIT));

	/* Should fail to enable */
	zassert_equal(-EIO, epacket_receive(epacket_dummy, K_FOREVER));
	zassert_false(epacket_dummy_receive_scheduled());
	zassert_equal(-EIO, epacket_receive(epacket_dummy, K_SECONDS(2)));
	zassert_false(epacket_dummy_receive_scheduled());

	epacket_dummy_receive_api_override(true, 0);
}

static struct net_buf *create_received_tdf_packet(uint8_t payload_len, bool encrypt)
{
	const bt_addr_le_t bt_addr_none = {0, {{0, 0, 0, 0, 0, 0}}};
	struct net_buf *buf_tx, *buf_rx;
	struct epacket_tx_metadata *tx_meta;
	struct epacket_rx_metadata *rx_meta;
	uint8_t *p;

	/* Construct the original TX packet */
	buf_tx = epacket_alloc_tx(K_NO_WAIT);
	zassert_not_null(buf_tx);
	net_buf_reserve(buf_tx, sizeof(struct epacket_bt_adv_frame));
	epacket_set_tx_metadata(buf_tx, EPACKET_AUTH_DEVICE, 0, INFUSE_TDF, EPACKET_ADDR_ALL);
	p = net_buf_add(buf_tx, 60);
	sys_rand_get(p, 60);

	if (encrypt) {
		zassert_equal(0, epacket_bt_adv_encrypt(buf_tx));
	}
	tx_meta = net_buf_user_data(buf_tx);

	/* Copy across to a received packet */
	buf_rx = epacket_alloc_rx(K_NO_WAIT);
	zassert_not_null(buf_rx);
	net_buf_add_mem(buf_rx, buf_tx->data, buf_tx->len);

	/* Add metadata */
	rx_meta = net_buf_user_data(buf_rx);
	rx_meta->interface = NULL;
	rx_meta->interface_id = EPACKET_INTERFACE_BT_ADV;
	rx_meta->interface_address.bluetooth = bt_addr_none;
	rx_meta->rssi = -80;
	rx_meta->flags = encrypt ? EPACKET_FLAGS_ENCRYPTION_DEVICE : 0x00;
	rx_meta->auth = encrypt ? EPACKET_AUTH_FAILURE : EPACKET_AUTH_DEVICE;

	/* Free the TX buffer */
	net_buf_unref(buf_tx);
	return buf_rx;
}

ZTEST(epacket_common, test_receive_append)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct net_buf *buf_rx, *buf_tx;
	uint16_t offset, len;
	int rc;

	buf_tx = epacket_alloc_tx_for_interface(epacket_dummy, K_NO_WAIT);
	zassert_not_null(buf_tx);

	/* Append packet that decrypted */
	buf_rx = create_received_tdf_packet(32, false);
	rc = epacket_received_packet_append(buf_tx, buf_rx);
	zassert_equal(0, rc);

	/* Append packet that did not decrypt */
	buf_rx = create_received_tdf_packet(32, true);
	rc = epacket_received_packet_append(buf_tx, buf_rx);
	zassert_equal(0, rc);

	/* Should be out of space */
	len = buf_tx->len;
	buf_rx = create_received_tdf_packet(32, true);
	rc = epacket_received_packet_append(buf_tx, buf_rx);
	zassert_equal(-ENOMEM, rc);
	zassert_equal(len, buf_tx->len);
	net_buf_unref(buf_rx);

	/* Basic validation of our format */
	offset = 0;
	/* First appended packet */
	len = sys_get_le16(buf_tx->data + offset);
	zassert_not_equal(0, len);
	zassert_equal(0x0000, len & 0x8000);
	offset += len & 0x7FFF;
	/* Second appended packet */
	len = sys_get_le16(buf_tx->data + offset);
	zassert_not_equal(0, len);
	zassert_equal(0x8000, len & 0x8000);
	offset += len & 0x7FFF;
	/* Should equal buffer size */
	zassert_equal(buf_tx->len, offset);

	net_buf_unref(buf_tx);
}

static bool security_init(const void *global_state)
{
	infuse_security_init();
	return true;
}

static void test_before(void *data)
{
	epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX);
}

ZTEST_SUITE(epacket_common, security_init, NULL, test_before, NULL, NULL);
