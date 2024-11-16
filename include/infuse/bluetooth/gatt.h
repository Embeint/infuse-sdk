/**
 * @file
 * @brief Infuse-IoT GATT helpers
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
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
struct bt_conn_auto_setup_params {
	/* Desired Bluetooth connection parameters */
	struct bt_le_conn_param conn_params;
	/* Duration to attempt to create connection for */
	uint32_t create_timeout_ms;
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
 * @brief Create a connection with automatic MTU update and characteristic discovery
 *
 * @param addr Remote device to connect to
 * @param conn Pointer to connection object allocated by @a bt_conn_le_create
 * @param params Connection configuration
 * @param discovery Characteristic discovery configuration
 *
 * @retval 0 Connection initiated (Result supplied through @a conn_setup_cb)
 * @retval -errno Error code from @a bt_conn_le_create
 */
int bt_conn_le_auto_setup(const bt_addr_le_t *addr, struct bt_conn **conn,
			  const struct bt_conn_auto_setup_params *params,
			  struct bt_conn_auto_discovery *discovery);

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
 * @brief Get last measured RSSI on a connection
 *
 * @param conn Bluetooth connection to query
 *
 * @return int8_t Last measured RSSI in dBm (0 before measurement)
 */
int8_t bt_conn_rssi(struct bt_conn *conn);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_GATT_H_ */
