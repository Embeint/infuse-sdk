/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <infuse/rpc/types.h>

/* Implementation of COAP_DOWNLOAD */
int rpc_command_coap_download_run(struct rpc_coap_download_request *req, char *resource,
				  struct rpc_coap_download_response *rsp, int *downloaded);
