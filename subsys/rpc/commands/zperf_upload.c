/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/zperf.h>
#include <zephyr/random/random.h>

#include <psa/crypto.h>

#include <infuse/data_logger/logger.h>
#include <infuse/epacket/keys.h>
#include <infuse/time/epoch.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/command_runner.h>
#include <infuse/rpc/types.h>
#include <infuse/security.h>

LOG_MODULE_DECLARE(rpc_server);

static int data_logger_read(const struct device *logger, uint64_t byte_offset, uint8_t *data,
			    uint32_t len)
{
	uint32_t block = byte_offset / 512;
	struct data_logger_state state;

	/* Simplify implementation by not considering unaligned sizes and offsets */
	if (((len != 512) && (len != 1024)) || (byte_offset % 512)) {
		return -EINVAL;
	}
	/* Ensure device initialised properly */
	if (!device_is_ready(logger)) {
		return -EBADF;
	}
	data_logger_get_state(logger, &state);
	if (state.block_size != 512) {
		return -EBADF;
	}
	if (state.current_block == 0) {
		return -EINVAL;
	}

	/* Loop over blocks that have actually been written.
	 * -1 to account for possible two block read.
	 */
	block %= (state.current_block - 1);
	return data_logger_block_read(logger, block, 0, data, len);
}

static int zperf_upload_data_loader(void *user_ctx, uint64_t offset, uint8_t *data, uint32_t len)
{
	enum rpc_enum_zperf_data_source *source = user_ctx;
	enum rpc_enum_zperf_data_source source_base =
		~RPC_ENUM_ZPERF_DATA_SOURCE_ENCRYPT & (*source);
	bool encrypt = !!(RPC_ENUM_ZPERF_DATA_SOURCE_ENCRYPT & *source);
	__maybe_unused const struct device *logger = NULL;
	size_t work_mem_size;
	int rc;

	uint8_t *storage = encrypt ? rpc_server_command_working_mem(&work_mem_size) : data;

	/* Populate the payload from the requested source */
	switch (source_base) {
	case RPC_ENUM_ZPERF_DATA_SOURCE_CONSTANT:
		memset(storage, 'i', len);
		break;
	case RPC_ENUM_ZPERF_DATA_SOURCE_RANDOM:
		sys_rand_get(storage, len);
		break;
#ifdef CONFIG_DATA_LOGGER_FLASH_MAP
	case RPC_ENUM_ZPERF_DATA_SOURCE_FLASH_ONBOARD:
		logger = DEVICE_DT_GET_ONE(embeint_data_logger_flash_map);
		break;
#endif /* CONFIG_DATA_LOGGER_FLASH_MAP */
#ifdef CONFIG_DATA_LOGGER_EXFAT
	case RPC_ENUM_ZPERF_DATA_SOURCE_FLASH_REMOVABLE:
		logger = DEVICE_DT_GET_ONE(embeint_data_logger_exfat);
		break;
#endif /* CONFIG_DATA_LOGGER_EXFAT */
	default:
		return -EINVAL;
	}

	if (logger != NULL) {
		/* Read from the logger */
		rc = data_logger_read(logger, offset, storage, len);
		if (rc < 0) {
			LOG_ERR("5 %d", rc);
			return rc;
		}
	}

	if (encrypt) {
		uint64_t epoch_seconds = epoch_time_seconds(epoch_time_now());
		psa_key_id_t psa_key_id;
		psa_status_t status;
		size_t out_len;

		if (work_mem_size < len) {
			return -EINVAL;
		}

		/* Use the default UDP network key for encryption */
		psa_key_id = epacket_key_id_get(EPACKET_KEY_NETWORK | EPACKET_KEY_INTERFACE_UDP,
						infuse_security_network_key_identifier(),
						epoch_seconds / SECONDS_PER_DAY);

		/* Ensure we don't reuse a nonce by randomising the first 12 bytes */
		sys_rand_get(storage, 12);

		/* Encrypt the packet.
		 * Since this is only for profiling purposes, overwrite the start and end of the
		 * buffer for the nonce and tag.
		 */
		status = psa_aead_encrypt(psa_key_id, PSA_ALG_CHACHA20_POLY1305, storage, 12, NULL,
					  0, storage + 12, len - 24, data, len, &out_len);
		if (status != PSA_SUCCESS) {
			return -EIO;
		}
	}
	return 0;
}

struct net_buf *rpc_command_zperf_upload(struct net_buf *request)
{
	struct epacket_rx_metadata *req_meta = net_buf_user_data(request);
	const struct device *interface = req_meta->interface;
	struct rpc_zperf_upload_request *req = (void *)request->data;
	enum rpc_enum_zperf_data_source source = req->data_source;
	struct rpc_zperf_upload_response rsp = {0};
	struct zperf_upload_params params = {0};
	struct zperf_results results = {0};
	uint8_t sock_type;
	uint64_t epoch_time;
	int rc;

	/* Peer address construction */
	if (IS_ENABLED(CONFIG_NET_IPV4) && (req->peer_address.sin_family == AF_INET)) {
		struct sockaddr_in *peer_addr = net_sin(&params.peer_addr);

		peer_addr->sin_family = AF_INET;
		peer_addr->sin_port = req->peer_address.sin_port;
		memcpy(peer_addr->sin_addr.s4_addr, req->peer_address.sin_addr,
		       sizeof(peer_addr->sin_addr.s4_addr));
	} else if (IS_ENABLED(CONFIG_NET_IPV6) && (req->peer_address.sin_family == AF_INET6)) {
		struct sockaddr_in6 *peer_addr = net_sin6(&params.peer_addr);

		peer_addr->sin6_family = AF_INET6;
		peer_addr->sin6_port = req->peer_address.sin_port;
		memcpy(peer_addr->sin6_addr.s6_addr, req->peer_address.sin_addr,
		       sizeof(peer_addr->sin6_addr.s6_addr));
	} else {
		LOG_WRN("%s type %d not supported", "Address", req->peer_address.sin_family);
		return rpc_response_simple_req(request, -EINVAL, &rsp, sizeof(rsp));
	}

	/* Upload request parameters */
	epoch_time = epoch_time_now();
	params.data_loader = zperf_upload_data_loader;
	params.data_loader_ctx = &source;
	params.unix_offset_us = (uint64_t)unix_time_from_epoch(epoch_time) * USEC_PER_SEC;
	params.unix_offset_us += epoch_time_milliseconds(epoch_time) * USEC_PER_MSEC;
	params.duration_ms = req->duration_ms;
	params.packet_size = req->packet_size;
	params.rate_kbps = req->rate_kbps ? req->rate_kbps : UINT32_MAX;
	params.options.report_interval_ms = 0;

	/* Free command as we no longer need it and this command can take a while */
	sock_type = req->sock_type;
	rpc_command_runner_request_unref(request);
	req = NULL;
	request = NULL;

	if (IS_ENABLED(CONFIG_NET_UDP) && (sock_type == SOCK_DGRAM)) {
		LOG_INF("Starting zperf %s upload", "UDP");
		rc = zperf_udp_upload(&params, &results);
	} else if (IS_ENABLED(CONFIG_NET_TCP) && (sock_type == SOCK_STREAM)) {
		LOG_INF("Starting zperf %s upload", "TCP");
		rc = zperf_tcp_upload(&params, &results);
	} else {
		LOG_WRN("%s type %d not supported", "Protocol", sock_type);
		return rpc_response_simple_if(interface, -EINVAL, &rsp, sizeof(rsp));
	}

	if (rc != 0) {
		rc = rc == -1 ? -errno : rc;
		LOG_ERR("Upload failed (%d)", rc);
		return rpc_response_simple_if(interface, rc, &rsp, sizeof(rsp));
	}
	LOG_INF("zperf upload complete");

	/* Copy results over to RPC response */
	rsp.nb_packets_sent = results.nb_packets_sent;
	rsp.nb_packets_rcvd = results.nb_packets_rcvd;
	rsp.nb_packets_lost = results.nb_packets_lost;
	rsp.nb_packets_outorder = results.nb_packets_outorder;
	rsp.total_len = results.total_len;
	rsp.time_in_us = results.time_in_us;
	rsp.jitter_in_us = results.jitter_in_us;
	rsp.client_time_in_us = results.client_time_in_us;
	rsp.packet_size = results.packet_size;
	rsp.nb_packets_errors = results.nb_packets_errors;

	/* Allocate and return the response */
	return rpc_response_simple_if(interface, 0, &rsp, sizeof(rsp));
}
