/*
SpIOpen Message Frame Publisher : Publisher handle and descriptor definitions.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include <cstdint>

namespace spiopen::message::publisher {

/**
 * @brief Publisher descriptor for diagnostics and context when publishing.
 *
 * Passed by publishers to higher-level code or used locally; the broker's
 * Publish() API takes only the message pointer.
 */
struct FramePublisherHandle_t {
    const char* name;     /**< Debug name used for diagnostics */
    uint16_t source_id;   /**< Source identifier for published messages */
    void* source_context; /**< Optional publisher-owned context pointer */
};

}  // namespace spiopen::message::publisher
