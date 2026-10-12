/*
 * Copyright (c) 2026 Embeint Holdings Pty Ltd
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <lvgl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <infuse/identifiers.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	struct display_capabilities capabilities;
	lv_obj_t *label;
	uint32_t refresh_ms;
	int rc;

	if (!device_is_ready(display)) {
		return -ENODEV;
	}
	display_get_capabilities(display, &capabilities);
	/* E-paper updates are expensive; avoid the LCD's continuously ticking UI. */
	refresh_ms = capabilities.screen_info & SCREEN_INFO_EPD ? 60000 : 1000;

#if DT_NODE_HAS_STATUS(DT_ALIAS(display_backlight), okay)
	const struct device *backlight = DEVICE_DT_GET(DT_PARENT(DT_ALIAS(display_backlight)));

	if (!device_is_ready(backlight)) {
		return -ENODEV;
	}
	rc = led_set_brightness(backlight, DT_NODE_CHILD_IDX(DT_ALIAS(display_backlight)), 25);
	if (rc != 0) {
		return rc;
	}
#endif

	/* Monochrome styling is readable on both colour LCD and e-paper panels. */
	lv_obj_set_style_bg_color(lv_screen_active(), lv_color_white(), 0);
	lv_obj_set_style_text_color(lv_screen_active(), lv_color_black(), 0);
	label = lv_label_create(lv_screen_active());
	lv_obj_set_width(label, capabilities.x_resolution);
	lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

	while (true) {
		lv_label_set_text_fmt(label, "Infuse-IoT\n\n%016llX\n\nUptime %lld s",
				      (unsigned long long)infuse_device_id(),
				      (long long)k_uptime_seconds());
		lv_timer_handler();
		rc = display_blanking_off(display);
		if (rc != 0 && rc != -ENOSYS) {
			return rc;
		}
		/* No animation or busy polling; idle time is available to the PM policy. */
		k_sleep(K_MSEC(refresh_ms));
	}
	return 0;
}
