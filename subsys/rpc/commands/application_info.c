/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>
#include <infuse/version.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/data_logger/logger.h>

#ifdef CONFIG_INFUSE_SECURITY
#include <infuse/security.h>
#endif

struct net_buf *rpc_command_application_info(struct net_buf *request)
{
	struct rpc_application_info_response rsp = {0};
	struct infuse_version version = application_version_get();

	/* Version information */
	rsp.application_id = CONFIG_INFUSE_APPLICATION_ID;
	rsp.version.major = version.major;
	rsp.version.minor = version.minor;
	rsp.version.revision = version.revision;
	rsp.version.build_num = version.build_num;

#ifdef CONFIG_DATA_LOGGER
	const struct device *logger_ext = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(data_logger_exfat));
	const struct device *logger_int = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(data_logger_flash));
	struct data_logger_state state;

	if ((logger_ext != NULL) && device_is_ready(logger_ext)) {
		data_logger_get_state(logger_ext, &state);
		rsp.data_blocks_external = state.current_block;
	}
	if ((logger_int != NULL) && device_is_ready(logger_int)) {
		data_logger_get_state(logger_int, &state);
		rsp.data_blocks_internal = state.current_block;
	}
#endif

#ifdef CONFIG_INFUSE_SECURITY
	rsp.network_id = infuse_security_network_key_identifier();
#endif
#ifdef CONFIG_KV_STORE
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots = {0};

	(void)KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	rsp.reboots = reboots.count;
	rsp.kv_crc = kv_store_reflect_crc();
#endif

	/* Other metadata */
	rsp.uptime = k_uptime_seconds();

	/* Allocate and return the response */
	return rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
}
