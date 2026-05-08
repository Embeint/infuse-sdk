/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/pm/device_runtime.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/auto/soc_device_control.h>
#include <infuse/zbus/channels.h>

static void new_battery_data(const struct zbus_channel *chan);

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_BATTERY);
ZBUS_LISTENER_DEFINE(battery_listener, new_battery_data);
ZBUS_CHAN_ADD_OBS(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_BATTERY), battery_listener, 10);

static sys_slist_t states = SYS_SLIST_STATIC_INIT(&states);
static struct k_spinlock list_lock;

int soc_device_control_register(struct soc_device_control_state *state)
{
	if ((state == NULL) || (state->soc_enable > 100) || (state->soc_disable > 100) ||
	    (state->soc_disable > state->soc_enable)) {
		return -EINVAL;
	}
	if (state->device == NULL) {
		return -ENODEV;
	}

	state->requested = false;
	K_SPINLOCK(&list_lock) {
		sys_slist_append(&states, &state->_node);
	}
	return 0;
}

bool soc_device_control_unregister(struct soc_device_control_state *state)
{
	bool removed;

	K_SPINLOCK(&list_lock) {
		removed = sys_slist_find_and_remove(&states, &state->_node);
		if (removed && state->requested) {
			pm_device_runtime_put(state->device);
		}
	}
	return removed;
}

static void process_new_battery_data(const struct tdf_battery_state *battery,
				     struct soc_device_control_state *state)
{
	if (state->requested && (battery->soc < state->soc_disable)) {
		pm_device_runtime_put(state->device);
		state->requested = false;
	}
	if (!state->requested && (battery->soc >= state->soc_enable)) {
		pm_device_runtime_get(state->device);
		state->requested = true;
	}
}

static void new_battery_data(const struct zbus_channel *chan)
{
	const struct tdf_battery_state *battery = zbus_chan_const_msg(chan);
	struct soc_device_control_state *state;

	K_SPINLOCK(&list_lock) {
		SYS_SLIST_FOR_EACH_CONTAINER(&states, state, _node) {
			process_new_battery_data(battery, state);
		}
	}
}
