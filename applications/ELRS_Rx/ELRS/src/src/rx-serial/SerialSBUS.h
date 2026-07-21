#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * SBUS receiver output (RX -> FC).
 *
 * Note: This implementation generates standard 25-byte SBUS frames. Electrical inversion
 * is a UART/board capability and is handled outside this module.
 */

uint32_t SerialSBUS_SendRCFrame(bool frameAvailable, bool frameMissed, uint32_t *channelData);
void SerialSBUS_processBytes(uint8_t *bytes, uint16_t size);
void SerialSBUS_queueMSPFrameTransmission(uint8_t *data);
void SerialSBUS_sendQueuedData(uint32_t maxBytesToSend);
