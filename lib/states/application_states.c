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

#include <infuse/states.h>

struct timeout_state {
	uint8_t state;
	uint16_t timeout;
};

static ATOMIC_DEFINE(application_states, INFUSE_STATES_END + 1);
static struct timeout_state timeout_states[CONFIG_INFUSE_APPLICATION_STATES_MAX_TIMEOUTS];
static uint32_t timeout_mask;
static struct k_spinlock timeout_lock;

BUILD_ASSERT(ARRAY_SIZE(application_states) == INFUSE_STATES_ARRAY_SIZE);

LOG_MODULE_REGISTER(states, CONFIG_INFUSE_APPLICATION_STATES_LOG_LEVEL);

void infuse_state_set(enum infuse_state state)
{
	atomic_set_bit(application_states, state);
	LOG_DBG("%d", state);
}

void infuse_state_set_timeout(enum infuse_state state, uint16_t timeout)
{
	if (timeout == 0) {
		return;
	}

	K_SPINLOCK(&timeout_lock) {
		if (__builtin_popcount(timeout_mask) == ARRAY_SIZE(timeout_states)) {
			LOG_WRN("Insufficient timeout contexts");
			K_SPINLOCK_BREAK;
		}
		uint8_t first_free = __builtin_ffs(~timeout_mask) - 1;

		atomic_set_bit(application_states, state);
		timeout_mask |= (1 << first_free);
		timeout_states[first_free].state = state;
		timeout_states[first_free].timeout = timeout;
	}
	LOG_DBG("%d for %d", state, timeout);
}

void infuse_state_clear(enum infuse_state state)
{
	atomic_clear_bit(application_states, state);
	LOG_DBG("%d", state);
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
	K_SPINLOCK(&timeout_lock) {
		uint32_t mask = timeout_mask;

		while (mask) {
			uint8_t first_used = __builtin_ffs(mask) - 1;

			if (atomic_test_bit(snapshot, timeout_states[first_used].state)) {
				if (--timeout_states[first_used].timeout == 0) {
					atomic_clear_bit(application_states,
							 timeout_states[first_used].state);
					timeout_mask ^= (1 << first_used);
					LOG_DBG("%d timed out", timeout_states[first_used].state);
				}
			}
			mask ^= 1 << first_used;
		}
	}
}
