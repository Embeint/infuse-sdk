/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/slist.h>

#include <infuse/states.h>

struct timeout_state {
	uint8_t state;
	uint16_t timeout;
};

static ATOMIC_DEFINE(application_states, INFUSE_STATES_END + 1);
static struct timeout_state timeout_states[CONFIG_INFUSE_APPLICATION_STATES_MAX_TIMEOUTS];
static uint32_t timeout_mask;
static struct k_spinlock timeout_lock;
static sys_slist_t cb_list = SYS_SLIST_STATIC_INIT(&cb_list);

BUILD_ASSERT(ARRAY_SIZE(application_states) == INFUSE_STATES_ARRAY_SIZE);

LOG_MODULE_REGISTER(states, CONFIG_INFUSE_APPLICATION_STATES_LOG_LEVEL);

void infuse_state_register_callback(struct infuse_state_cb *cb)
{
	sys_slist_append(&cb_list, &cb->node);
}

bool infuse_state_unregister_callback(struct infuse_state_cb *cb)
{
	return sys_slist_find_and_remove(&cb_list, &cb->node);
}

static uint8_t find_timeout_state(enum infuse_state state)
{
	/* Find and remove any pending timeouts */
	uint32_t mask = timeout_mask;

	while (mask) {
		uint8_t first_used = __builtin_ffs(mask) - 1;

		if (timeout_states[first_used].state == state) {
			return first_used;
		}
		mask ^= BIT(first_used);
	}
	return UINT8_MAX;
}

static void clear_timeout_state(enum infuse_state state)
{
	uint8_t timeout_idx = find_timeout_state(state);

	if (timeout_idx == UINT8_MAX) {
		return;
	}
	/* Clear all timeout state */
	timeout_states[timeout_idx].state = 0;
	timeout_states[timeout_idx].timeout = 0;
	timeout_mask ^= BIT(timeout_idx);
}

bool infuse_state_set(enum infuse_state state)
{
	struct infuse_state_cb *cb;
	bool already_set = false;

	K_SPINLOCK(&timeout_lock) {
		already_set = atomic_test_and_set_bit(application_states, state);
		if (already_set == true) {
			clear_timeout_state(state);
		}
		/* Notify registered callbacks */
		SYS_SLIST_FOR_EACH_CONTAINER(&cb_list, cb, node) {
			if (cb->state_set) {
				cb->state_set(state, already_set, 0, cb->user_ctx);
			}
		}
	}
	LOG_DBG("%d", state);
	return already_set;
}

bool infuse_state_set_timeout(enum infuse_state state, uint16_t timeout)
{
	struct infuse_state_cb *cb;
	bool already_set = false;

	if (timeout == 0) {
		return false;
	}

	K_SPINLOCK(&timeout_lock) {
		uint8_t timeout_idx = find_timeout_state(state);

		if (timeout_idx == UINT8_MAX) {
			if (__builtin_popcount(timeout_mask) == ARRAY_SIZE(timeout_states)) {
				LOG_WRN("Insufficient timeout contexts");
				K_SPINLOCK_BREAK;
			}
			timeout_idx = __builtin_ffs(~timeout_mask) - 1;
			already_set = atomic_test_and_set_bit(application_states, state);
			timeout_mask |= BIT(timeout_idx);
			timeout_states[timeout_idx].state = state;
		} else {
			already_set = true;
		}
		timeout_states[timeout_idx].timeout = timeout;
		/* Notify registered callbacks */
		SYS_SLIST_FOR_EACH_CONTAINER(&cb_list, cb, node) {
			if (cb->state_set) {
				cb->state_set(state, already_set, timeout, cb->user_ctx);
			}
		}
	}
	LOG_DBG("%d for %d", state, timeout);
	return already_set;
}

int infuse_state_get_timeout(enum infuse_state state)
{
	uint8_t timeout_idx;
	int rc = 0;

	K_SPINLOCK(&timeout_lock) {
		if (atomic_test_bit(application_states, state)) {
			timeout_idx = find_timeout_state(state);
			if (timeout_idx == UINT8_MAX) {
				/* No timeout */
				rc = 0;
			} else {
				rc = timeout_states[timeout_idx].timeout;
			}
		} else {
			rc = -EINVAL;
		}
	}
	return rc;
}

bool infuse_state_clear(enum infuse_state state)
{
	struct infuse_state_cb *cb;
	bool was_set = false;

	K_SPINLOCK(&timeout_lock) {
		was_set = atomic_test_and_clear_bit(application_states, state);
		if (was_set == true) {
			clear_timeout_state(state);

			/* Notify registered callbacks */
			SYS_SLIST_FOR_EACH_CONTAINER(&cb_list, cb, node) {
				if (cb->state_cleared) {
					cb->state_cleared(state, cb->user_ctx);
				}
			}
		}
	}
	LOG_DBG("%d", state);
	return was_set;
}

bool infuse_state_get(enum infuse_state state)
{
	return atomic_test_bit(application_states, state);
}

void infuse_states_snapshot(atomic_t snapshot[ATOMIC_BITMAP_SIZE(INFUSE_STATES_END + 1)])
{
	for (int i = 0; i < INFUSE_STATES_ARRAY_SIZE; i++) {
		snapshot[i] = atomic_get(&application_states[i]);
	}
}

void infuse_states_tick(atomic_t snapshot[INFUSE_STATES_ARRAY_SIZE])
{
	struct infuse_state_cb *cb;

	K_SPINLOCK(&timeout_lock) {
		uint32_t mask = timeout_mask;

		while (mask) {
			uint8_t first_used = __builtin_ffs(mask) - 1;
			uint8_t state = timeout_states[first_used].state;

			mask ^= BIT(first_used);

			if (!atomic_test_bit(snapshot, state)) {
				continue;
			}
			if (--timeout_states[first_used].timeout > 0) {
				continue;
			}
			atomic_clear_bit(application_states, state);
			timeout_mask ^= BIT(first_used);
			LOG_DBG("%d timed out", state);
			/* Notify registered callbacks */
			SYS_SLIST_FOR_EACH_CONTAINER(&cb_list, cb, node) {
				if (cb->state_cleared) {
					cb->state_cleared(state, cb->user_ctx);
				}
			}
			timeout_states[first_used].state = 0;
		}
	}
}
