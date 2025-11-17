/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_LED_LP581X_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_LED_LP581X_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Advanced LP581x LED Controller configuration
 * @defgroup lp581x_apis LP581x LED Controller API
 * @{
 */

/** Maximum number of LED channels */
#define LP581X_MAX_LEDS 4

enum lp581x_engine {
	LP581X_ENGINE_0 = 0,
	LP581X_ENGINE_1 = 1,
	LP581X_ENGINE_2 = 2,
	LP581X_ENGINE_3 = 3,
	/** Number of available engines */
	LP581X_NUM_ENGINES,
} __packed;

enum lp581x_pattern {
	LP581X_PATTERN_0 = 0,
	LP581X_PATTERN_1 = 1,
	LP581X_PATTERN_2 = 2,
	LP581X_PATTERN_3 = 3,
	/** Number of available patterns */
	LP581X_NUM_PATTERNS,
	/** Skip pattern in this step */
	LP581X_PATTERN_SKIP = LP581X_NUM_PATTERNS,
} __packed;

/** Duration of an pattern phase */
enum lp581x_phase_duration {
	/** No phase duration */
	LP581X_PHASE_0_MS = 0x0,
	/** 50ms duration */
	LP581X_PHASE_50_MS = 0x1,
	/** 100ms duration */
	LP581X_PHASE_100_MS = 0x2,
	/** 150ms duration */
	LP581X_PHASE_150_MS = 0x3,
	/** 200ms duration */
	LP581X_PHASE_200_MS = 0x4,
	/** 250ms duration */
	LP581X_PHASE_250_MS = 0x5,
	/** 300ms duration */
	LP581X_PHASE_300_MS = 0x6,
	/** 350ms duration */
	LP581X_PHASE_350_MS = 0x7,
	/** 400ms duration */
	LP581X_PHASE_400_MS = 0x8,
	/** 450ms duration */
	LP581X_PHASE_450_MS = 0x9,
	/** 500ms duration */
	LP581X_PHASE_500_MS = 0xA,
	/** 1000ms duration */
	LP581X_PHASE_1000_MS = 0xB,
	/** 2000ms duration */
	LP581X_PHASE_2000_MS = 0xC,
	/** 4000ms duration */
	LP581X_PHASE_4000_MS = 0xD,
	/** 6000ms duration */
	LP581X_PHASE_6000_MS = 0xE,
	/** 8000ms duration */
	LP581X_PHASE_8000_MS = 0xF,
	/** End marker */
	_LP581X_PHASE_END,
} __packed;

/** Repeat the pattern forever */
#define LP581X_PATTERN_PLAY_FOREVER 0xF

/**
 * @brief LED pattern configuration
 *
 * Pattern:
 *   Static PWM output (pre_pause)
 *   N runs of varying output (sloper)
 *   Static PWM output (post_pause)
 *
 * Refer to datasheet section 7.3.4.1 for explanatory graphics
 */
struct lp581x_animation_pattern {
	/** Pause before pattern starts running */
	struct {
		/** LED intensity in pre-pause */
		uint8_t pwm;
		/** Duration of pre-pause */
		enum lp581x_phase_duration duration;
	} pre_pause;
	/** Sloper configuration steps */
	struct {
		/** Number of times to run, 0 to 14 or @ref LP581X_PATTERN_PLAY_FOREVER */
		uint8_t play_count;
		/** Brightness of sloper steps */
		uint8_t pwm[3];
		/** Duration of sloper steps */
		enum lp581x_phase_duration duration[4];
	} sloper;
	/** Pause after pattern stops running */
	struct {
		/** LED intensity in psot-pause */
		uint8_t pwm;
		/** Duration of psot-pause */
		enum lp581x_phase_duration duration;
	} post_pause;
};

/**
 * @brief Program an animation pattern to the LP581X
 *
 * The LP581X devices have 4 programmable patterns, with each pattern being
 * available to any of the animation engines. Any number of animation engines
 * can use each pattern at the same time.
 *
 * Animation patterns must be programmed before starting the animation.
 *
 * @param dev LP581X device
 * @param pattern_idx Pattern to program, 0 to 3
 * @param pattern Pattern configuration
 *
 * @retval 0 On success
 * @retval -ENOTSUP Part variant does not support animations
 * @retval -EINVAL If @a pattern_idx or @a pattern have invalid values
 * @retval -EBUSY If animation engines are already running
 * @retval -errno On failure
 */
int lp581x_animation_pattern_program(const struct device *dev, uint8_t pattern_idx,
				     const struct lp581x_animation_pattern *pattern);

/** Number of times the animation engine repeats the configured patterns */
enum lp581x_animation_engine_repeats {
	/** Engine does not repeat the configured pattern (one total run) */
	LP581X_ENGINE_REPEAT_NONE = 0,
	/** Engine repeats the configured pattern once (two total runs) */
	LP581X_ENGINE_REPEAT_ONCE = 1,
	/** Engine repeats the configured pattern twice (three total runs) */
	LP581X_ENGINE_REPEAT_TWICE = 2,
	/** Engine repeats the configured pattern forever */
	LP581X_ENGINE_REPEAT_FOREVER = 3,
} __packed;

/** Configuration of a single engine */
struct lp581x_animation_engine_config {
	/** Pattern order */
	enum lp581x_pattern order[4];
	/** Number of times to repeat array of patterns */
	enum lp581x_animation_engine_repeats repeats;
};

/** Configuration of all engines */
struct lp581x_animation_engines_config {
	/** Animation engine that drives each LED channel */
	enum lp581x_engine led_channel_engines[LP581X_MAX_LEDS];
	/** Number of animation engines configured by @a engines */
	uint8_t num_engines;
	/** Configuration of each animation engine */
	struct lp581x_animation_engine_config engines[LP581X_NUM_ENGINES];
};

/**
 * @brief Configure LP581X animation engines
 *
 * The LP581X devices have 4 independent programmable animation engines. Each
 * engine has 4 steps, where each step runs a pattern programed by
 * @ref lp581x_animation_pattern_program.
 *
 * Animation engines must be programmed before starting the animation.
 *
 * @param dev LP581X device
 * @param config Engine configuration
 *
 * @retval 0 On success
 * @retval -ENOTSUP Part variant does not support animations
 * @retval -EINVAL If @a config has invalid values
 * @retval -EBUSY If animation engines are already running
 * @retval -errno On failure
 */
int lp581x_animation_engines_configure(const struct device *dev,
				       const struct lp581x_animation_engines_config *config);

/**
 * @brief Start animations
 *
 * @param dev LP581X device
 * @param led_bitmask Bitmask of LEDs that should be enabled
 *
 * @retval 0 On success
 * @retval -ENOTSUP Part variant does not support animations
 * @retval -EINVAL If @a led_bitmask has invalid values
 * @retval -EBUSY If animation engines are already running
 * @retval -errno On failure
 */
int lp581x_animation_start(const struct device *dev, uint8_t led_bitmask);

/**
 * @brief Stop animations
 *
 * @param dev LP581X device
 *
 * @retval 0 On success
 * @retval -ENOTSUP Part variant does not support animations
 * @retval -EAGAIN Animations are not currently running
 * @retval -errno On failure
 */
int lp581x_animation_stop(const struct device *dev);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_LED_LP581X_H_ */
