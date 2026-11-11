/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net/net_if.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>

#include "common.h"

LOG_MODULE_DECLARE(infuse_coap, CONFIG_INFUSE_COAP_LOG_LEVEL);

#define COAP_PKT_SIZE(payload_size) ((payload_size) + COAP_RSP_OVERHEAD)

#ifdef CONFIG_NET_IPV4_MTU
/* If the MTU is explicitly defined, check the size before trying to use 1KB blocks */
#define MTU_SUPPORTS_1KB (CONFIG_NET_IPV4_MTU >= COAP_PKT_SIZE(1024))
#else
/* Assume it is supported until a configuration proves otherwise */
#define MTU_SUPPORTS_1KB 1
#endif

/* Determine the locations of '/' characters and encode into array */
int ic_resource_path_split(const char *resource, uint16_t *component_starts, uint8_t array_len)
{
	uint8_t num_paths = 1;
	size_t i = 0;

	/* Scan through string for the '/' character */
	while (true) {
		if ((resource[i] == '/') || (resource[i] == '\0')) {
			if ((i - component_starts[num_paths - 1]) > COAP_URI_PATH_SEGMENT_MAX_LEN) {
				/* Segment too long */
				return -EINVAL;
			}
			if (resource[i] == '\0') {
				break;
			}
			if (num_paths == (array_len - 1)) {
				/* Too many path splits */
				return -EINVAL;
			}
			/* Store the start of the next component */
			component_starts[num_paths++] = i + 1;
		}
		i++;
	}
	/* Add the end of the string with a hypothetical next component */
	component_starts[num_paths] = i + 1;
	return num_paths;
}

/* Append resource path using splits from `resource_path_split` */
int ic_resource_path_append(struct coap_packet *request, const char *resource,
			    const uint16_t *component_starts, uint8_t num_components)
{
	int rc;

	/* Add all path components to packet */
	for (int i = 0; i < num_components; i++) {
		const char *comp_start = resource + component_starts[i];
		int comp_len = component_starts[i + 1] - component_starts[i] - 1;

		rc = coap_packet_append_option(request, COAP_OPTION_URI_PATH, comp_start, comp_len);
		if (rc < 0) {
			return rc;
		}
	}
	return 0;
}

int ic_get_block_size(size_t working_size, uint16_t block_size, enum coap_block_size *used_size)
{
	bool supports_1kb = MTU_SUPPORTS_1KB;
	struct net_if *iface;
	uint16_t iface_mtu;

	switch (block_size) {
	case 1024:
		*used_size = COAP_BLOCK_1024;
		break;
	case 512:
		*used_size = COAP_BLOCK_512;
		break;
	case 256:
		*used_size = COAP_BLOCK_256;
		break;
	case 128:
		*used_size = COAP_BLOCK_128;
		break;
	case 64:
		*used_size = COAP_BLOCK_64;
		break;
	case 32:
		*used_size = COAP_BLOCK_32;
		break;
	case 16:
		*used_size = COAP_BLOCK_16;
		break;
	case 0:
		/* Dynamically check the networking interface MTU if possible */
		iface = net_if_get_default();
		if (iface != NULL) {
			iface_mtu = net_if_get_mtu(iface);
			if (iface_mtu > 0) {
				/* Interface MTU is known, override the kconfig based decision */
				supports_1kb = iface_mtu > COAP_PKT_SIZE(1024);
				/* Also limit the buffer size */
				working_size = MIN(working_size, iface_mtu);
			}
		}
		/* Automatically determine block size */
		if (supports_1kb && (working_size >= COAP_PKT_SIZE(1024))) {
			*used_size = COAP_BLOCK_1024;
		} else if (working_size >= COAP_PKT_SIZE(512)) {
			*used_size = COAP_BLOCK_512;
		} else if (working_size >= COAP_PKT_SIZE(256)) {
			*used_size = COAP_BLOCK_256;
		} else if (working_size >= COAP_PKT_SIZE(128)) {
			*used_size = COAP_BLOCK_128;
		} else {
			*used_size = COAP_BLOCK_64;
		}
		break;
	default:
		return -EINVAL;
	}

	/* Working memory must be able to receive a complete block response */
	if (working_size < (coap_block_size_to_bytes(*used_size) + COAP_RSP_MIN_OVERHEAD)) {
		return -ENOMEM;
	}
	return 0;
}

void ic_ack_if_confirmable(int socket, const struct coap_packet *response)
{
	uint8_t ack_buf[4];
	struct coap_packet ack;
	int rc;

	if (coap_header_get_type(response) != COAP_TYPE_CON) {
		return;
	}

	/* Empty ACK (no token) with the message ID of the response */
	rc = coap_packet_init(&ack, ack_buf, sizeof(ack_buf), COAP_VERSION_1, COAP_TYPE_ACK, 0,
			      NULL, COAP_CODE_EMPTY, coap_header_get_id(response));
	if (rc == 0) {
		rc = zsock_send(socket, ack.data, ack.offset, 0);
	}
	if (rc < 0) {
		/* Not fatal, the server will retransmit the response */
		LOG_WRN("Failed to ACK response (%d)", rc < -1 ? rc : -errno);
	}
}
