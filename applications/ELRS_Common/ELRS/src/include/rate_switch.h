#pragma once

#include <stdint.h>

// Full-resolution SYNC extension in two formerly reserved bytes. PREPARE
// announces the desired model/rate without moving the receiver off-channel.
// COMMIT is repeated on the old rate before both peers apply the new rate.
#define OTA_RATE_PREPARE 0xA1U
#define OTA_RATE_COMMIT  0xA2U
#define OTA_RATE_COMMIT_WINDOW_MS 200U
#define OTA_RATE_TX_GUARD_MS 200U
#define OTA_RATE_COMMIT_TICK_MS 10U

static inline uint8_t OtaRateCommitTicks(uint32_t remainingMs)
{
    uint32_t ticks = (remainingMs + OTA_RATE_COMMIT_TICK_MS - 1U) / OTA_RATE_COMMIT_TICK_MS;
    return (uint8_t)(ticks > 255U ? 255U : ticks);
}
