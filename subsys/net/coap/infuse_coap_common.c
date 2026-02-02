/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include "common.h"

#ifdef CONFIG_NET_IPV4_MTU
/* If the MTU is explicitly defined, check the size before trying to use 1KB blocks */
#define MTU_SUPPORTS_1KB (CONFIG_NET_IPV4_MTU >= (1024 + 64))
#else
/* Assume it is supported until a configuration proves otherwise */
#define MTU_SUPPORTS_1KB 1
#endif

/* Determine the locations of '/' characters and encode into array */
int ic_resource_path_split(const char *resource, uint8_t *component_starts, uint8_t array_len)
{
	uint8_t num_paths = 1;
	int i = 0;

	/* Scan through string for the '/' character */
	while (resource[i] != '\0') {
		if (resource[i] == '/') {
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
			    uint8_t *component_starts, uint8_t num_components)
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
		/* Automatically determine block size */
		if (MTU_SUPPORTS_1KB && (working_size >= (1024 + 64))) {
			*used_size = COAP_BLOCK_1024;
		} else if (working_size >= (512 + 64)) {
			*used_size = COAP_BLOCK_512;
		} else if (working_size >= (256 + 64)) {
			*used_size = COAP_BLOCK_256;
		} else if (working_size >= (128 + 64)) {
			*used_size = COAP_BLOCK_128;
		} else if (working_size >= 128) {
			*used_size = COAP_BLOCK_64;
		} else {
			return -ENOMEM;
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
