/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_SHARED_DEVICE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_SHARED_DEVICE_H_

#include <stdint.h>
#include <stdbool.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Shared device API
 * @defgroup shared_device_apis Shared device APIs
 * @{
 */

/**
 * @brief Container for shared device information specified in devicetree
 *
 * This type contains a pointer to a shared device, the state that is requested,
 * and the priority of that state request.
 *
 * @see SHARED_DEVICE_DT_SPEC_GET_BY_IDX
 * @see SHARED_DEVICE_DT_SPEC_GET_BY_IDX_OR
 * @see SHARED_DEVICE_DT_SPEC_GET
 * @see SHARED_DEVICE_DT_SPEC_GET_OR
 */
struct shared_device_dt_spec {
	/* Pointer to the shared device */
	const struct device *shared;
	/* State that is being requested */
	uint8_t state;
	/* Priority of the state request */
	uint8_t priority;
};

/**
 * @brief Static initializer for a @p shared_device_dt_spec
 *
 *  * Example devicetree fragment:
 *
 *	n: node {
 *		rf-switch = <&ant_switch 0 100>,
 *			    <&ant_switch 1 50>;
 *	}
 *
 * Example usage:
 *
 *	const struct shared_device_dt_spec spec = GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(n),
 *								          rf-switch, 1);
 *	// Initializes 'spec' to:
 *	// {
 *	//         .shared = DEVICE_DT_GET(DT_NODELABEL(ant_switch)),
 *	//         .state = 1,
 *	//         .priority = 50
 *	// }
 *
 * The 'shared' field must still be checked for readiness, e.g. using
 * device_is_ready(). It is an error to use this macro unless the node
 * exists, has the given property, and that property specifies a shared
 * device, requested state, and state priority as shown above.
 *
 * @param node_id devicetree node identifier
 * @param prop lowercase-and-underscores property name
 * @param idx logical index into "prop"
 * @return static initializer for a struct shared_device_dt_spec for the property
 */
#define SHARED_DEVICE_DT_SPEC_GET_BY_IDX(node_id, prop, idx)                                       \
	{                                                                                          \
		.shared = DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx)),                    \
		.state = DT_PHA_BY_IDX(node_id, prop, idx, state),                                 \
		.priority = DT_PHA_BY_IDX(node_id, prop, idx, priority),                           \
	}

/**
 * @brief Like SHARED_DEVICE_DT_SPEC_GET_BY_IDX(), with a fallback to a default value
 *
 * If the devicetree node identifier 'node_id' refers to a node with a
 * property 'prop', this expands to
 * <tt>SHARED_DEVICE_DT_SPEC_GET_BY_IDX(node_id, prop, idx)</tt>.
 * The @p default_value parameter is not expanded in this case.
 *
 * Otherwise, this expands to @p default_value.
 *
 * @param node_id devicetree node identifier
 * @param prop lowercase-and-underscores property name
 * @param idx logical index into "prop"
 * @param default_value fallback value to expand to
 * @return static initializer for a struct shared_device_dt_spec for the property,
 *         or default_value if the node or property do not exist
 */
#define SHARED_DEVICE_DT_SPEC_GET_BY_IDX_OR(node_id, prop, idx, default_value)                     \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, prop),                                               \
		    (SHARED_DEVICE_DT_SPEC_GET_BY_IDX(node_id, prop, idx)), (default_value))

/**
 * @brief Equivalent to SHARED_DEVICE_DT_SPEC_GET_BY_IDX(node_id, prop, 0).
 *
 * @param node_id devicetree node identifier
 * @param prop lowercase-and-underscores property name
 * @return static initializer for a struct shared_device_dt_spec for the property
 * @see SHARED_DEVICE_DT_SPEC_GET_BY_IDX()
 */
#define SHARED_DEVICE_DT_SPEC_GET(node_id, prop) SHARED_DEVICE_DT_SPEC_GET_BY_IDX(node_id, prop, 0)

/**
 * @brief Equivalent to
 *        SHARED_DEVICE_DT_SPEC_GET_BY_IDX_OR(node_id, prop, 0, default_value).
 *
 * @param node_id devicetree node identifier
 * @param prop lowercase-and-underscores property name
 * @param default_value fallback value to expand to
 * @return static initializer for a struct shared_device_dt_spec for the property
 * @see SHARED_DEVICE_DT_SPEC_GET_BY_IDX_OR()
 */
#define SHARED_DEVICE_DT_SPEC_GET_OR(node_id, prop, default_value)                                 \
	SHARED_DEVICE_DT_SPEC_GET_BY_IDX_OR(node_id, prop, 0, default_value)

/**
 * @brief Static initializer for a @p shared_device_dt_spec from a DT_DRV_COMPAT
 * instance's shared device property at an index.
 *
 * @param inst DT_DRV_COMPAT instance number
 * @param prop lowercase-and-underscores property name
 * @param idx logical index into "prop"
 * @return static initializer for a struct shared_device_dt_spec for the property
 * @see SHARED_DEVICE_DT_SPEC_GET_BY_IDX()
 */
#define SHARED_DEVICE_DT_SPEC_INST_GET_BY_IDX(inst, prop, idx)                                     \
	SHARED_DEVICE_DT_SPEC_GET_BY_IDX(DT_DRV_INST(inst), prop, idx)

/**
 * @brief Static initializer for a @p shared_device_dt_spec from a DT_DRV_COMPAT
 *        instance's shared device property at an index, with fallback
 *
 * @param inst DT_DRV_COMPAT instance number
 * @param prop lowercase-and-underscores property name
 * @param idx logical index into "prop"
 * @param default_value fallback value to expand to
 * @return static initializer for a struct shared_device_dt_spec for the property
 * @see SHARED_DEVICE_DT_SPEC_GET_BY_IDX_OR()
 */
#define SHARED_DEVICE_DT_SPEC_INST_GET_BY_IDX_OR(inst, prop, idx, default_value)                   \
	SHARED_DEVICE_DT_SPEC_GET_BY_IDX_OR(DT_DRV_INST(inst), prop, idx, default_value)

/**
 * @brief Equivalent to SHARED_DEVICE_DT_SPEC_INST_GET_BY_IDX(inst, prop, 0).
 *
 * @param inst DT_DRV_COMPAT instance number
 * @param prop lowercase-and-underscores property name
 * @return static initializer for a struct shared_device_dt_spec for the property
 * @see SHARED_DEVICE_DT_SPEC_INST_GET_BY_IDX()
 */
#define SHARED_DEVICE_DT_SPEC_INST_GET(inst, prop)                                                 \
	SHARED_DEVICE_DT_SPEC_INST_GET_BY_IDX(inst, prop, 0)

/**
 * @brief Equivalent to
 *        SHARED_DEVICE_DT_SPEC_INST_GET_BY_IDX_OR(inst, prop, 0, default_value).
 *
 * @param inst DT_DRV_COMPAT instance number
 * @param prop lowercase-and-underscores property name
 * @param default_value fallback value to expand to
 * @return static initializer for a struct shared_device_dt_spec for the property
 * @see SHARED_DEVICE_DT_SPEC_INST_GET_BY_IDX_OR()
 */
#define SHARED_DEVICE_DT_SPEC_INST_GET_OR(inst, prop, default_value)                               \
	SHARED_DEVICE_DT_SPEC_INST_GET_BY_IDX_OR(inst, prop, 0, default_value)

struct shared_device_api {
	int (*request)(const struct device *dev, uint8_t state_priority, uint8_t state);
	int (*release)(const struct device *dev, uint8_t state_priority);
};

/**
 * @brief Validate that the shared device is ready.
 *
 * @param spec Shared device specification from devicetree
 *
 * @retval true if the shared device is ready for use.
 * @retval false if the shared device is not ready for use.
 */
static inline bool shared_device_is_ready_dt(const struct shared_device_dt_spec *spec)
{
	if (spec->shared == NULL) {
		return true;
	}
	return device_is_ready(spec->shared);
}

/**
 * @brief Request a device to be in a given state
 *
 * The state with the highest requested priority is the one active.
 * The behaviour when no state is selected is implementation-defined.
 *
 * @param dev Shared device to request to be in a certain state
 * @param state_priority Priority of the request, each state must have a single associated priority
 * @param state State that is being requested for a device
 *
 * @retval 0 request has been submitted
 * @retval -EALREADY a request for the given state already exists
 * @retval -EINVAL state is invalid for this device
 */
static inline int shared_device_request(const struct device *dev, uint8_t state_priority,
					uint8_t state)
{
	const struct shared_device_api *api = dev->api;

	return api->request(dev, state_priority, state);
}

/**
 * @brief Request a shared device from a @p shared_device_dt_spec.
 *
 * This is equivalent to:
 *
 *     shared_device_request(spec->shared, spec->priority, spec->state);
 *
 * @param spec Shared device specification from devicetree
 *
 * @return a value from shared_device_request()
 */
static inline int shared_device_request_dt(const struct shared_device_dt_spec *spec)
{
	if (spec->shared == NULL) {
		return 0;
	}
	return shared_device_request(spec->shared, spec->priority, spec->state);
}

/**
 * @brief Release a previous request for a given state
 *
 * @param dev Switch to release
 * @param state_priority Priority of the request provided to @ref shared_device_request
 *
 * @retval 0 request has been released
 * @retval -EINVAL no request was previously received
 */
static inline int shared_device_release(const struct device *dev, uint8_t state_priority)
{
	const struct shared_device_api *api = dev->api;

	return api->release(dev, state_priority);
}

/**
 * @brief Release a shared device from a @p shared_device_dt_spec.
 *
 * This is equivalent to:
 *
 *     shared_device_release(spec->shared, spec->priority);
 *
 * @param spec Shared device specification from devicetree
 *
 * @return a value from shared_device_release()
 */
static inline int shared_device_release_dt(const struct shared_device_dt_spec *spec)
{
	if (spec->shared == NULL) {
		return 0;
	}
	return shared_device_release(spec->shared, spec->priority);
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_SHARED_DEVICE_H_ */
