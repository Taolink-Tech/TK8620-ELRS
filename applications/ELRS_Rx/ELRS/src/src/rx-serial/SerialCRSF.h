#include <stdbool.h>
#include <stdint.h>

uint32_t SerialCRSF_SendRCFrame(bool frameAvailable, bool frameMissed, uint32_t *channelData);
void SerialCRSF_sendQueuedData(uint32_t maxBytesToSend);
void SerialCRSF_processBytes(uint8_t *bytes, uint16_t size);
void SerialCRSF_queueMSPFrameTransmission(uint8_t* data);
