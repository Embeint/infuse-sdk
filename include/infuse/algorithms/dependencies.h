/**
 * @file
 * @brief Application symbols that algorithms can use
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_DEPENDENCIES_H_
#define INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_DEPENDENCIES_H_

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#ifdef CONFIG_ZBUS
/* If being compiled natively, we need inline definitions available */
#include <zephyr/zbus/zbus.h>
#endif /* CONFIG_ZBUS */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Functions that Infuse-IoT algorithms can use
 * @defgroup infuse_algorithm_dependencies Infuse-IoT Algorithm Dependencies
 * @{
 */

/* zbus.h: Opaque channel pointer */
struct zbus_channel;

/** zbus.h: See @a zbus_chan_from_id */
const struct zbus_channel *zbus_chan_from_id(uint32_t channel_id);

#ifndef CONFIG_ZBUS
/** zbus.h: See @a zbus_chan_const_msg */
const void *zbus_chan_const_msg(const struct zbus_channel *chan);
#endif /* !CONFIG_ZBUS */

/** zbus.h: @a zbus_chan_read with K_NO_WAIT timeout */
int zbus_chan_read_no_wait(const struct zbus_channel *chan, void *msg);

/** zbus.h: See @a zbus_chan_finish */
int zbus_chan_finish(const struct zbus_channel *chan);

/** printk.h: See @a printk */
void printk(const char *fmt, ...);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_DEPENDENCIES_H_ */
