/**
 * @file
 * @brief Infuse-IoT GATT helpers
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_GATT_H_
#define INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_GATT_H_

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/addr.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup infuse_gatt_apis Infuse-IoT GATT APIs
 * @{
 */

/**
 * @brief Remote GATT Characteristic information
 *
 * Fields are discovered on the first connection, and re-used as long
 * as the GATT DB hash of the remote device remains constant.
 */
struct bt_gatt_remote_char {
	/* Start handle for the characteristic */
	uint16_t attr_start_handle;
	/* End handle for the characteristic */
	uint16_t attr_end_handle;
	/* Handle for the value attribute */
	uint16_t value_handle;
	/* Handle for the Client Characteristic Configuration (0 if doesn't exist) */
	uint16_t ccc_handle;
	/* Characteristic properties (BT_GATT_CHRC_) */
	uint8_t properties;
};

/**
 * @brief Parameters for automatically setup connection
 */
struct bt_conn_auto_setup_cb {
	/* Run when connection has been successfully setup or failed */
	void (*conn_setup_cb)(struct bt_conn *conn, int err, void *user_data);
	/* Run when connection has terminated, if @a conn_setup_cb has previously run */
	void (*conn_terminated_cb)(struct bt_conn *conn, int reason, void *user_data);
	/* User data provided to callbacks */
	void *user_data;
};

/**
 * @brief Database cache to speed up repeat connections
 */
struct bt_conn_auto_database_cache {
	/* Cached GATT database hash value */
	uint8_t db_hash[16];
	/* Pointer to list of cached remote characteristics */
	struct bt_gatt_remote_char *remote_info;
	/* Access spinlock */
	struct k_spinlock lock;
};

/**
 * @brief Create a cache variable that holds a given number of characteristics
 *
 * @param name Name of the created variable
 * @param num_characteristics Number of characteristics cache can hold
 */
#define BT_CONN_AUTO_CACHE(name, num_characteristics)                                              \
	static struct bt_gatt_remote_char name##_storage[num_characteristics];                     \
	static struct bt_conn_auto_database_cache name = {                                         \
		.remote_info = name##_storage,                                                     \
	}

/**
 * @brief Characteristics to discover on the connection
 */
struct bt_conn_auto_discovery {
	/* List of UUIDs to discover */
	const struct bt_uuid **characteristics;
	/* Cached characteristics from previous connections */
	struct bt_conn_auto_database_cache *cache;
	/* Pointer to list of characteristics to discover */
	struct bt_gatt_remote_char *remote_info;
	/* Pending database hash */
	uint8_t db_hash_pending[16];
	/* Number of characteristics to discover */
	uint8_t num_characteristics;
};

/**
 * @brief Setup a connection with automatic MTU exchange and characteristic discovery
 *
 * This should be called immediately following @a bt_conn_le_create to ensure
 * that the context is setup correctly by the time the connection establishes.
 *
 * @param conn Connection object allocated by @a bt_conn_le_create
 * @param discovery Characteristic discovery configuration
 * @param callbacks Callbacks to call on connection/disconnection
 * @param preferred_phy Bitmask of preferred PHYs to use for the connection `BT_GAP_LE_PHY_*`
 */
void bt_conn_le_auto_setup(struct bt_conn *conn, struct bt_conn_auto_discovery *discovery,
			   const struct bt_conn_auto_setup_cb *callbacks, uint8_t preferred_phy);

/**
 * @brief Trigger a disconnection and wait for it to complete
 *
 * @param conn Connection object to disconnect
 *
 * @retval 0 on success
 * @retval -errno Error code @a bt_conn_disconnect
 */
int bt_conn_disconnect_sync(struct bt_conn *conn);

/**
 * @brief Wait for a connection to disconnect, without initiating it
 *
 * @param conn Connection object to wait for
 * @param timeout Duration to wait for
 *
 * @retval 0 on success
 * @retval -errno Error code @a bt_conn_disconnect
 */
int bt_conn_disconnect_wait(struct bt_conn *conn, k_timeout_t timeout);

/**
 * @brief Get last measured RSSI on a connection
 *
 * @param conn Bluetooth connection to query
 *
 * @return int8_t Last measured RSSI in dBm (0 before measurement)
 */
int8_t bt_conn_rssi(struct bt_conn *conn);

/**
 * @brief Start logging the RSSI of the connection
 *
 * This state only persists until the connection is lost.
 * This function must therefore be called each time the connection is established.
 *
 * @param conn Bluetooth connection to log
 * @param tdf_loggers TDF loggers to log RSSI to
 */
void bt_conn_rssi_log(struct bt_conn *conn, uint8_t tdf_loggers);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_GATT_H_ */
