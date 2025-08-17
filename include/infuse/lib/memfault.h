/**
 * @file
 * @brief Infuse-IoT Memfault integration
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_LIB_MEMFAULT_H_
#define INFUSE_SDK_INCLUDE_INFUSE_LIB_MEMFAULT_H_

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup memfault_apis Infuse-IoT Memfault APIs
 * @{
 */

/**
 * @brief Header for Memfault chunks in a binary container
 *
 * This is used as the Memfault packetizer will only populate a single chunk
 * type into a memfault_packetizer_get_chunk call, even if two consecutive chunks
 * would fit into the single buffer. This header allows us to be as efficient as
 * possible with a given packet/block size.
 */
struct memfault_chunk_header {
	uint16_t chunk_len;
	uint8_t chunk_cnt;
} __packed;

/**
 * @brief Send as many pending Memfault chunks over an ePacket interface as possible
 *
 * @param dev ePacket interface to dump to
 *
 * @retval true When chunk dumping has completed
 * @retval false When function needs to be called again shortly due to buffer starvation
 */
bool infuse_memfault_dump_chunks_epacket(const struct device *dev);

/**
 * @brief Dump all chunks to the default ePacket interface
 *
 * @note The chosen interface is defined by the infuse,memfault-epacket-dump chosen node
 *
 * @param delay Delay before starting dump process
 *
 * @retval 0 When chunk dump has been queued successfully
 * @retval -ENOTCONN if interface is not connected
 * @retval -ENODATA if no chunks are pending
 */
int infuse_memfault_queue_dump_all(k_timeout_t delay);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_LIB_MEMFAULT_H_ */
