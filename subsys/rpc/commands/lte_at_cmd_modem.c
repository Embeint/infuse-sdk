/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>

#include <zephyr/modem/at/user_pipe.h>

static struct modem_chat modem_chat_ctx;
static uint8_t lte_at_chat_receive_buf[128];
static uint8_t *lte_at_chat_argv_buf[2];
struct ctx {
	char *buf;
	size_t len;
	int rc;
};

LOG_MODULE_DECLARE(rpc_server);

void match_copy(struct ctx *cmd_ctx, char *str, bool newline)
{
	/* Copy characters out while we have space in the output buffer.
	 * Don't use strncpy since we would need to iterate over the string again to
	 * find the new length for the next iteration.
	 */
	while (*str != '\0' && cmd_ctx->len) {
		*cmd_ctx->buf = *str;
		cmd_ctx->buf++;
		cmd_ctx->len--;
		str++;
	}
	/* Insert newline to separate lines in the output */
	if (newline && cmd_ctx->len) {
		*cmd_ctx->buf = '\n';
		cmd_ctx->buf++;
		cmd_ctx->len--;
	}
}

void partial_callback(struct modem_chat *chat, char **argv, uint16_t argc, void *user_data)
{
	/* Copy out the last element, which contains the partial match */
	match_copy(user_data, argv[argc - 1], true);
}

void match_callback(struct modem_chat *chat, char **argv, uint16_t argc, void *user_data)
{
	/* Copy out the match components */
	match_copy(user_data, argv[0], argc == 1);
	if (argc > 1) {
		/* Copy any trailing info */
		match_copy(user_data, argv[1], true);
	}
}

static void script_done_cb(struct modem_chat *chat, enum modem_chat_script_result result,
			   void *user_data)
{
	struct ctx *cmd_ctx = user_data;

	switch (result) {
	case MODEM_CHAT_SCRIPT_RESULT_SUCCESS:
		cmd_ctx->rc = 0;
		break;
	case MODEM_CHAT_SCRIPT_RESULT_ABORT:
		cmd_ctx->rc = -ECANCELED;
		break;
	case MODEM_CHAT_SCRIPT_RESULT_TIMEOUT:
		cmd_ctx->rc = -EAGAIN;
		break;
	}
}

static struct modem_chat_match chat_matches[2] = {
	MODEM_CHAT_MATCH_INITIALIZER("", "", partial_callback, false, true),
	MODEM_CHAT_MATCH("OK", "", match_callback),
};
static struct modem_chat_match abort_matches[2] = {
	MODEM_CHAT_MATCH("+CME ERROR", "", match_callback),
	MODEM_CHAT_MATCH("ERROR", "", match_callback),
};

int cellular_modem_at_cmd(void *buf, size_t len, const char *cmd)
{
	struct modem_chat_script_chat chat_single;
	struct modem_chat_script script;
	struct ctx cmd_ctx = {
		.buf = buf,
		/* Reserve one byte for the NULL terminator */
		.len = len - 1,
	};

	modem_chat_script_chat_init(&chat_single);
	modem_chat_script_chat_set_request(&chat_single, cmd);
	modem_chat_script_chat_set_response_matches(&chat_single, chat_matches, 2);
	modem_chat_script_chat_set_timeout(&chat_single, 2000);

	modem_chat_script_init(&script);
	modem_chat_script_set_script_chats(&script, &chat_single, 1);
	modem_chat_script_set_callback(&script, script_done_cb);
	modem_chat_script_set_abort_matches(&script, abort_matches, 1);
	modem_chat_script_set_timeout(&script, 2);

	cmd_ctx.rc = modem_at_user_pipe_claim();
	if (cmd_ctx.rc) {
		*cmd_ctx.buf = '\0';
		return cmd_ctx.rc;
	}

	modem_chat_ctx.user_data = &cmd_ctx;
	cmd_ctx.rc = modem_chat_run_script(&modem_chat_ctx, &script);

	/* Add NULL terminator */
	*cmd_ctx.buf = '\0';

	modem_at_user_pipe_release();
	return cmd_ctx.rc;
}

static int lte_at_cmd_modem_init(void)
{
	struct modem_chat_config chat_cfg = {
		.receive_buf = lte_at_chat_receive_buf,
		.receive_buf_size = sizeof(lte_at_chat_receive_buf),
		.delimiter = "\r",
		.delimiter_size = sizeof("\r") - 1,
		.filter = "\n",
		.filter_size = sizeof("\n") - 1,
		.argv = lte_at_chat_argv_buf,
		.argv_size = ARRAY_SIZE(lte_at_chat_argv_buf),
	};

	modem_chat_init(&modem_chat_ctx, &chat_cfg);
	modem_at_user_pipe_init(&modem_chat_ctx);
	return 0;
}

SYS_INIT(lte_at_cmd_modem_init, POST_KERNEL, 99);
