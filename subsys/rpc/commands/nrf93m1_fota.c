/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/modem/modem_cellular.h>
#include <zephyr/logging/log.h>
#include <zephyr/modem/at/user_pipe.h>
#include <zephyr/modem/chat.h>
#include <zephyr/net_buf.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/errors.h>
#include <infuse/rpc/types.h>

#define NRF93M1_FOTA_URL_MAX_LEN 255
#define NRF93M1_FOTA_CMD_MAX_LEN (sizeof("AT%HTTPFOTADL=\"\",100") + NRF93M1_FOTA_URL_MAX_LEN)
#define NRF93M1_FOTA_TIMEOUT_SEC (30 * 60)

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

struct fota_ctx {
	struct rpc_nrf93m1_fota_response *rsp;
	int rc;
};

static struct modem_chat fota_chat;
static uint8_t fota_chat_receive_buf[CONFIG_MODEM_CELLULAR_USER_PIPE_BUFFER_SIZES];
static uint8_t *fota_chat_argv_buf[4];

static void fota_verified_callback(struct modem_chat *chat, char **argv, uint16_t argc,
				   void *user_data)
{
	struct fota_ctx *ctx = user_data;

	ARG_UNUSED(chat);
	ARG_UNUSED(argv);
	ARG_UNUSED(argc);

	ctx->rc = 0;
}

static void fota_error_callback(struct modem_chat *chat, char **argv, uint16_t argc,
				void *user_data)
{
	struct fota_ctx *ctx = user_data;

	ARG_UNUSED(chat);

	if (argc >= 3) {
		ctx->rsp->cause = strtol(argv[1], NULL, 10);
		ctx->rsp->detail = strtol(argv[2], NULL, 10);
	}
	ctx->rc = INFUSE_RPC_ERROR_MODEM_AT_FAILED;
}

static void fota_progress_callback(struct modem_chat *chat, char **argv, uint16_t argc,
				   void *user_data)
{
	ARG_UNUSED(chat);
	ARG_UNUSED(argv);
	ARG_UNUSED(argc);
	ARG_UNUSED(user_data);

	/* FOTA still progressing */
	rpc_server_watchdog_feed();
}

static void fota_script_done_cb(struct modem_chat *chat, enum modem_chat_script_result result,
				void *user_data)
{
	struct fota_ctx *ctx = user_data;

	ARG_UNUSED(chat);

	switch (result) {
	case MODEM_CHAT_SCRIPT_RESULT_SUCCESS:
		break;
	case MODEM_CHAT_SCRIPT_RESULT_ABORT:
		ctx->rc = INFUSE_RPC_ERROR_MODEM_AT_CANCELLED;
		break;
	case MODEM_CHAT_SCRIPT_RESULT_TIMEOUT:
		ctx->rc = INFUSE_RPC_ERROR_MODEM_AT_TIMEOUT;
		break;
	}
}

static bool valid_url(const char *url)
{
	size_t len = strlen(url);

	if ((len == 0) || (len > NRF93M1_FOTA_URL_MAX_LEN)) {
		return false;
	}
	if ((strncmp(url, "http://", sizeof("http://") - 1) != 0) &&
	    (strncmp(url, "https://", sizeof("https://") - 1) != 0)) {
		return false;
	}
	return strpbrk(url, "\"\r\n") == NULL;
}

static int run_fota(struct fota_ctx *ctx, const char *url)
{
	struct modem_chat_match ok_match = MODEM_CHAT_MATCH("OK", "", NULL);
	struct modem_chat_match final_matches[] = {
		MODEM_CHAT_MATCH_INITIALIZER("%HTTPURC: \"FOTA\",\"DOWNLOADING\",", ",",
					     fota_progress_callback, false, true),
		MODEM_CHAT_MATCH("%HTTPURC: \"FOTA\",\"VERIFIED\"", "", fota_verified_callback),
	};
	struct modem_chat_match abort_matches[] = {
		MODEM_CHAT_MATCH("+CME ERROR", "", NULL),
		MODEM_CHAT_MATCH("ERROR", "", NULL),
		MODEM_CHAT_MATCH("%HTTPURC: \"FOTA\",\"DOWNLOAD ERROR\",", ",",
				 fota_error_callback),
		MODEM_CHAT_MATCH("%HTTPURC: \"FOTA\",\"VERIFICATION FAILURE\",", ",",
				 fota_error_callback),
	};
	struct modem_chat_script_chat chats[2];
	struct modem_chat_script script;
	char cmd[NRF93M1_FOTA_CMD_MAX_LEN];
	int rc;

	rc = snprintk(cmd, sizeof(cmd), "AT%%HTTPFOTADL=\"%s\",100", url);
	if ((rc < 0) || (rc >= (int)sizeof(cmd))) {
		return INFUSE_RPC_ERROR_MALFORMED_REQUEST;
	}

	modem_chat_script_chat_init(&chats[0]);
	modem_chat_script_chat_set_request(&chats[0], cmd);
	modem_chat_script_chat_set_response_matches(&chats[0], &ok_match, 1);
	modem_chat_script_chat_set_timeout(&chats[0], 2000);

	modem_chat_script_chat_init(&chats[1]);
	modem_chat_script_chat_set_request(&chats[1], "");
	modem_chat_script_chat_set_response_matches(&chats[1], final_matches,
						    ARRAY_SIZE(final_matches));

	modem_chat_script_init(&script);
	modem_chat_script_set_script_chats(&script, chats, ARRAY_SIZE(chats));
	modem_chat_script_set_callback(&script, fota_script_done_cb);
	modem_chat_script_set_abort_matches(&script, abort_matches, ARRAY_SIZE(abort_matches));
	modem_chat_script_set_timeout(&script, NRF93M1_FOTA_TIMEOUT_SEC + 2);

	rc = modem_at_user_pipe_claim(&fota_chat, K_MSEC(200));
	if (rc) {
		return INFUSE_RPC_ERROR_MODEM_AT_BUSY;
	}

	fota_chat.user_data = ctx;
	ctx->rc = INFUSE_RPC_ERROR_MODEM_AT_FAILED;
	rc = modem_chat_run_script(&fota_chat, &script);
	if (rc < 0) {
		ctx->rc = INFUSE_RPC_ERROR_MODEM_AT_FAILED;
	}

	modem_at_user_pipe_release();
	return ctx->rc;
}

struct net_buf *rpc_command_nrf93m1_fota(struct net_buf *request)
{
	struct net_if *iface = net_if_get_first_by_type(&(NET_L2_GET_NAME(PPP)));
	struct rpc_nrf93m1_fota_request *req = (void *)request->data;
	struct rpc_nrf93m1_fota_response rsp = {0};
	struct fota_ctx ctx = {
		.rsp = &rsp,
	};
	int rc;

	if (request->data[request->len - 1] != '\0' || !valid_url(req->url)) {
		return rpc_response_simple_req(request, INFUSE_RPC_ERROR_MALFORMED_REQUEST, &rsp,
					       sizeof(rsp));
	}

	/* The FOTA script completes once the download process terminates.
	 * If completed successfully, the modem reboots and reports %FOTA events.
	 * Once %FOTA reports done (%FOTA, "DONE"), the modem reports its standard "RDY".
	 */
	rc = run_fota(&ctx, req->url);
	if (rc == 0) {
		/* Modem takes some time to start the FOTA process.
		 * After a few seconds, it reports '+CEREG: 0' then closes the PPP link with a
		 * `Terminate-Request` packet. Unfortunately after that there is no other
		 * notification to hook into, in particular no CMUX closing event.
		 *
		 * Therefore the best we can do at this point is wait until the PPP link goes
		 * down and hope that is the right point to move the modem into the rebooting state.
		 *
		 * Experimentally, there is just over a second between the PPP close and the first
		 * %FOTA notification.
		 */
		LOG_INF("Waiting for interface down");
		for (int i = 0; i < 500; i++) {
			if (!net_if_is_carrier_ok(iface)) {
				LOG_INF("Interface down after %d ms", 10 * i);
				break;
			}
			k_sleep(K_MSEC(10));
		}
		/* Notify the cellular driver that the modem is now rebooting */
		modem_cellular_notify_modem_rebooting(DEVICE_DT_GET(DT_ALIAS(modem)));
	}

	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}

static int nrf93m1_fota_init(void)
{
	struct modem_chat_config chat_cfg = {
		.receive_buf = fota_chat_receive_buf,
		.receive_buf_size = sizeof(fota_chat_receive_buf),
		.delimiter = "\r",
		.delimiter_size = sizeof("\r") - 1,
		.filter = "\n",
		.filter_size = sizeof("\n") - 1,
		.argv = fota_chat_argv_buf,
		.argv_size = ARRAY_SIZE(fota_chat_argv_buf),
	};

	modem_chat_init(&fota_chat, &chat_cfg);
	return 0;
}

SYS_INIT(nrf93m1_fota_init, POST_KERNEL, 99);
