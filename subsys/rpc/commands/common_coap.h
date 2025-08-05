/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <infuse/rpc/types.h>

/* Implementation of COAP_DOWNLOAD */
int rpc_command_coap_download_run(struct rpc_coap_download_request *req, char *resource,
				  struct rpc_coap_download_response *rsp, int *downloaded);
