/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
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
static sys_slist_t cb_list = SYS_SLIST_STATIC_INIT(&cb_list);
/* Serialises state updates and their notifications. Callbacks run with this held, so they may
 * block, and as the mutex is recursive they may also update states themselves.
 */
static K_MUTEX_DEFINE(state_lock);

BUILD_ASSERT(ARRAY_SIZE(application_states) == INFUSE_STATES_ARRAY_SIZE);

LOG_MODULE_REGISTER(states, CONFIG_INFUSE_APPLICATION_STATES_LOG_LEVEL);

void infuse_state_register_callback(struct infuse_state_cb *cb)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	sys_slist_append(&cb_list, &cb->node);
	k_mutex_unlock(&state_lock);
}

bool infuse_state_unregister_callback(struct infuse_state_cb *cb)
{
	bool removed;

	k_mutex_lock(&state_lock, K_FOREVER);
	removed = sys_slist_find_and_remove(&cb_list, &cb->node);
	k_mutex_unlock(&state_lock);
	return removed;
}

static void notify_set(enum infuse_state state, bool already, uint16_t timeout)
{
	struct infuse_state_cb *cb, *cbs;

	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&cb_list, cb, cbs, node) {
		if (cb->state_set) {
			cb->state_set(state, already, timeout, cb->user_ctx);
		}
	}
}

static void notify_cleared(enum infuse_state state)
{
	struct infuse_state_cb *cb, *cbs;

	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&cb_list, cb, cbs, node) {
		if (cb->state_cleared) {
			cb->state_cleared(state, cb->user_ctx);
		}
	}
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
	bool already_set;

	k_mutex_lock(&state_lock, K_FOREVER);
	already_set = atomic_test_and_set_bit(application_states, state);
	if (already_set == true) {
		clear_timeout_state(state);
	}
	notify_set(state, already_set, 0);
	k_mutex_unlock(&state_lock);
	LOG_DBG("%d", state);
	return already_set;
}

bool infuse_state_set_timeout(enum infuse_state state, uint16_t timeout)
{
	bool already_set = false;
	uint8_t timeout_idx;

	if (timeout == 0) {
		return false;
	}

	k_mutex_lock(&state_lock, K_FOREVER);
	timeout_idx = find_timeout_state(state);
	if (timeout_idx == UINT8_MAX) {
		if (__builtin_popcount(timeout_mask) == ARRAY_SIZE(timeout_states)) {
			LOG_WRN("Insufficient timeout contexts");
			goto unlock;
		}
		timeout_idx = __builtin_ffs(~timeout_mask) - 1;
		already_set = atomic_test_and_set_bit(application_states, state);
		timeout_mask |= BIT(timeout_idx);
		timeout_states[timeout_idx].state = state;
	} else {
		already_set = true;
	}
	timeout_states[timeout_idx].timeout = timeout;
	notify_set(state, already_set, timeout);
	LOG_DBG("%d for %d", state, timeout);
unlock:
	k_mutex_unlock(&state_lock);
	return already_set;
}

int infuse_state_get_timeout(enum infuse_state state)
{
	uint8_t timeout_idx;
	int rc;

	k_mutex_lock(&state_lock, K_FOREVER);
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
	k_mutex_unlock(&state_lock);
	return rc;
}

bool infuse_state_clear(enum infuse_state state)
{
	bool was_set;

	k_mutex_lock(&state_lock, K_FOREVER);
	was_set = atomic_test_and_clear_bit(application_states, state);
	if (was_set == true) {
		clear_timeout_state(state);
		notify_cleared(state);
	}
	k_mutex_unlock(&state_lock);
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
	uint8_t expired[ARRAY_SIZE(timeout_states)];
	uint8_t num_expired = 0;
	uint32_t mask;

	k_mutex_lock(&state_lock, K_FOREVER);
	/* Update all timeouts before notifying, as callbacks can modify the timeouts */
	mask = timeout_mask;
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
		timeout_states[first_used].state = 0;
		expired[num_expired++] = state;
		LOG_DBG("%d timed out", state);
	}
	/* Notify registered callbacks */
	for (int i = 0; i < num_expired; i++) {
		notify_cleared(expired[i]);
	}
	k_mutex_unlock(&state_lock);
}
