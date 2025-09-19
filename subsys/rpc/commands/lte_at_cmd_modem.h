/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_SUBSYS_RPC_COMMANDS_LTE_AT_CMD_MODEM_H_
#define INFUSE_SDK_SUBSYS_RPC_COMMANDS_LTE_AT_CMD_MODEM_H_

#include <stddef.h>

/**
 * @brief Send a formatted AT command to the modem
 *	  and receive the response into the supplied buffer.
 *
 * @param buf Buffer to receive the response into.
 * @param len Buffer length.
 * @param cmd Command buffer (with required terminating characters)
 */
int cellular_modem_at_cmd(void *buf, size_t len, const char *cmd);

#endif /* INFUSE_SDK_SUBSYS_RPC_COMMANDS_LTE_AT_CMD_MODEM_H_ */
