#ifndef H_CRSF
#define H_CRSF

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "crsf_protocol.h"
#include "telemetry_protocol.h"
#include "msp.h"
#include "msptypes.h"

typedef struct {
    elrsLinkStatistics_t LinkStatistics; // Link Statistics Stored as Struct
    uint8_t MspData[ELRS_MSP_BUFFER];
    uint8_t MspDataLength;
    bool HasUpdatedUplinkPower;
} CRSF_t;

void CRSF_GetMspMessage(uint8_t **data, uint8_t *len);
void CRSF_UnlockMspMessage(void);
void CRSF_AddMspMessage_len(uint8_t length, uint8_t *data);
void CRSF_AddMspMessage_packet(mspPacket_t *packet, uint8_t destination);
void CRSF_ResetMspQueue(void);
void CRSF_GetDeviceInformation(uint8_t *frame, uint8_t fieldCount);
void CRSF_SetMspV2Request(uint8_t *frame, uint16_t function, uint8_t *payload, uint8_t payloadLength);
void CRSF_SetHeaderAndCrc(uint8_t *frame, crsf_frame_type_e frameType, uint8_t frameSize, crsf_addr_e destAddr);
void CRSF_SetExtendedHeaderAndCrc(uint8_t *frame, crsf_frame_type_e frameType, uint8_t frameSize, crsf_addr_e senderAddr, crsf_addr_e destAddr);
uint32_t CRSF_VersionStrToU32(const char *verStr);
void CRSF_updateUplinkPower(uint8_t uplinkPower);
bool CRSF_clearUpdatedUplinkPower(void);
elrsLinkStatistics_t *CRSF_GetLinkStatistics(void);

#endif
