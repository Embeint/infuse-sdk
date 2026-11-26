/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * Control channel to the pytest shims (scripts/pylib/infuse_pytest/cloud_shims.py)
 */

#ifndef INFUSE_TESTS_EPACKET_INTERFACES_PYTEST_SHIM_H_
#define INFUSE_TESTS_EPACKET_INTERFACES_PYTEST_SHIM_H_

#include <string.h>

#include <zephyr/net/socket.h>
#include <zephyr/ztest.h>

/**
 * @brief Send a command to the pytest shims, and wait for it to be applied
 *
 * @param cmd Command string, e.g. "cloud_drop 1"
 */
static inline void pytest_shim_command(const char *cmd)
{
	struct net_sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(CONFIG_TEST_SHIM_CONTROL_PORT),
	};
	const struct timeval timeout = {.tv_sec = 1};
	char rsp[4];
	int sock, rc;

	zassert_equal(1, zsock_inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr));
	sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	zassert_true(sock >= 0);
	zassert_equal(0,
		      zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));

	rc = zsock_sendto(sock, cmd, strlen(cmd), 0, (struct net_sockaddr *)&addr, sizeof(addr));
	zassert_equal(strlen(cmd), rc, "Failed to send '%s' (%d)", cmd, errno);
	rc = zsock_recv(sock, rsp, sizeof(rsp), 0);
	(void)zsock_close(sock);
	zassert_equal(2, rc, "No response to '%s' (%d)", cmd, errno);
	zassert_mem_equal("OK", rsp, 2, "Shim rejected '%s'", cmd);
}

#endif /* INFUSE_TESTS_EPACKET_INTERFACES_PYTEST_SHIM_H_ */
