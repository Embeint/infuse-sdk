/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdio.h>

#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt.h>

#include <zcbor_common.h>
#include <zcbor_encode.h>
#include <zcbor_decode.h>

#include <mgmt/mcumgr/util/zcbor_bulk.h>

#include <infuse/time/epoch.h>
#include <infuse/reboot.h>

static int os_mgmt_echo(struct smp_streamer *ctxt)
{
	zcbor_state_t *zsd = ctxt->reader->zs;
	zcbor_state_t *zse = ctxt->writer->zs;
	struct zcbor_string data = {0};
	size_t decoded;
	bool ok;

	struct zcbor_map_decode_key_val echo_decode[] = {
		ZCBOR_MAP_DECODE_KEY_DECODER("d", zcbor_tstr_decode, &data),
	};

	ok = zcbor_map_decode_bulk(zsd, echo_decode, ARRAY_SIZE(echo_decode), &decoded) == 0;

	if (!ok) {
		return MGMT_ERR_EINVAL;
	}

	ok = zcbor_tstr_put_lit(zse, "r") && zcbor_tstr_encode(zse, &data);

	return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

static int os_mgmt_datetime_read(struct smp_streamer *ctxt)
{
	zcbor_state_t *zse = ctxt->writer->zs;
	uint64_t now = epoch_time_now();
	char date_string[32];
	struct tm cal;
	uint32_t ms;
	bool ok;

	epoch_time_unix_calendar(now, &cal);
	ms = k_epoch_to_ms_near32(epoch_time_subseconds(now));

	snprintf(date_string, sizeof(date_string), "%4d-%02d-%02dT%02d:%02d:%02d.%03d",
		 (uint16_t)(cal.tm_year + 1900), (uint8_t)(cal.tm_mon + 1), (uint8_t)cal.tm_mday,
		 (uint8_t)cal.tm_hour, (uint8_t)cal.tm_min, (uint8_t)cal.tm_sec, ms);

	ok = zcbor_tstr_put_lit(zse, "datetime") &&
	     zcbor_tstr_encode_ptr(zse, date_string, strlen(date_string));

	return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

static int os_mgmt_reset(struct smp_streamer *ctxt)
{
	infuse_reboot_delayed(INFUSE_REBOOT_MCUMGR, 0x00, 0x00, K_MSEC(2000));
	return MGMT_ERR_EOK;
}

static int os_mgmt_mcumgr_params(struct smp_streamer *ctxt)
{
	zcbor_state_t *zse = ctxt->writer->zs;
	bool ok;

	ok = zcbor_tstr_put_lit(zse, "buf_size") &&
	     zcbor_uint32_put(zse, CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE) &&
	     zcbor_tstr_put_lit(zse, "buf_count") &&
	     zcbor_uint32_put(zse, CONFIG_MCUMGR_TRANSPORT_NETBUF_COUNT);

	return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

static const struct mgmt_handler os_mgmt_group_handlers[] = {
	[OS_MGMT_ID_ECHO] = {os_mgmt_echo, os_mgmt_echo},
	[OS_MGMT_ID_DATETIME_STR] = {os_mgmt_datetime_read, NULL},
	[OS_MGMT_ID_RESET] = {NULL, os_mgmt_reset},
	[OS_MGMT_ID_MCUMGR_PARAMS] = {os_mgmt_mcumgr_params, NULL},
};

static struct mgmt_group os_mgmt_group = {
	.mg_handlers = os_mgmt_group_handlers,
	.mg_handlers_count = ARRAY_SIZE(os_mgmt_group_handlers),
	.mg_group_id = MGMT_GROUP_ID_OS,
};

static void os_mgmt_register_group(void)
{
	mgmt_register_group(&os_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(os_mgmt, os_mgmt_register_group);
