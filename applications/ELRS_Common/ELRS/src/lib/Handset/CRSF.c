#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "CRSF.h"
#include "options.h"
#include "FIFO.h"
#include "common.h"
#include "crsf_protocol.h"
#include "crc.h"

static CRSF_t CRSF = {0};

FIFO_t MspWriteFIFO = {0};

/***
 * @brief: Convert `version` (string) to a integer version representation
 * e.g. "2.2.15 ISM24G" => 0x0002020f
 * Assumes all version fields are < 256, the number portion
 * MUST be followed by a space to correctly be parsed
 ***/
uint32_t CRSF_VersionStrToU32(const char *verStr)
{
    uint32_t retVal = 0;
#if !defined(FORCE_NO_DEVICE_VERSION)
    uint8_t accumulator = 0;
    char c;
    bool trailing_data = false;
    while ((c = *verStr))
    {
        ++verStr;
        // A decimal indicates moving to a new version field
        if (c == '.')
        {
            retVal = (retVal << 8) | accumulator;
            accumulator = 0;
            trailing_data = false;
        }
        // Else if this is a number add it up
        else if (c >= '0' && c <= '9')
        {
            accumulator = (accumulator * 10) + (c - '0');
            trailing_data = true;
        }
        // Anything except [0-9.] ends the parsing
        else
        {
            break;
        }
    }
    if (trailing_data)
    {
        retVal = (retVal << 8) | accumulator;
    }
    // If the version ID is < 1.0.0, we could not parse it,
    // just use the OTA version as the major version number
    if (retVal < 0x010000)
    {
        retVal = OTA_VERSION_ID << 16;
    }
#endif
    return retVal;
}

void CRSF_GetDeviceInformation(uint8_t *frame, uint8_t fieldCount)
{
    const uint8_t size = strlen(device_name)+1;
    deviceInformationPacket_t *device = (deviceInformationPacket_t *)(frame + sizeof(crsf_ext_header_t) + size);
    // Packet starts with device name
    memcpy(frame + sizeof(crsf_ext_header_t), device_name, size);
    // Followed by the device
    device->serialNo = htobe32(0x454C5253); // ['E', 'L', 'R', 'S'], seen [0x00, 0x0a, 0xe7, 0xc6] // "Serial 177-714694" (value is 714694)
    device->hardwareVer = 0; // unused currently by us, seen [ 0x00, 0x0b, 0x10, 0x01 ] // "Hardware: V 1.01" / "Bootloader: V 3.06"
    device->softwareVer = htobe32(CRSF_VersionStrToU32(version)); // seen [ 0x00, 0x00, 0x05, 0x0f ] // "Firmware: V 5.15"
    device->fieldCnt = fieldCount;
    device->parameterVersion = 0;
}

void CRSF_SetMspV2Request(uint8_t *frame, uint16_t function, uint8_t *payload, uint8_t payloadLength)
{
    uint8_t *packet = (uint8_t *)(frame + sizeof(crsf_ext_header_t));
    packet[0] = 0x50;          // no error, version 2, beginning of the frame, first frame (0)
    packet[1] = 0;             // flags
    packet[2] = function & 0xFF;
    packet[3] = (function >> 8) & 0xFF;
    packet[4] = payloadLength & 0xFF;
    packet[5] = (payloadLength >> 8) & 0xFF;
    memcpy(packet + 6, payload, payloadLength);
    packet[6 + payloadLength] = CalcCRCMsp(packet + 1, payloadLength + 5); // crc = flags + function + length + payload
}

void CRSF_SetHeaderAndCrc(uint8_t *frame, crsf_frame_type_e frameType, uint8_t frameSize, crsf_addr_e destAddr)
{
    crsf_header_t *header = (crsf_header_t *)frame;
    header->device_addr = destAddr;
    header->frame_size = frameSize;
    header->type = frameType;

    uint8_t crc = GENERIC_CRC8Calc(&frame[CRSF_FRAME_NOT_COUNTED_BYTES], frameSize - 1, 0);
    frame[frameSize + CRSF_FRAME_NOT_COUNTED_BYTES - 1] = crc;
}

void CRSF_SetExtendedHeaderAndCrc(uint8_t *frame, crsf_frame_type_e frameType, uint8_t frameSize, crsf_addr_e senderAddr, crsf_addr_e destAddr)
{
    crsf_ext_header_t *header = (crsf_ext_header_t *)frame;
    header->dest_addr = destAddr;
    header->orig_addr = senderAddr;
    CRSF_SetHeaderAndCrc(frame, frameType, frameSize, destAddr);
}


void CRSF_GetMspMessage(uint8_t **data, uint8_t *len)
{
    *len = CRSF.MspDataLength;
    *data = (CRSF.MspDataLength > 0) ? CRSF.MspData : NULL;
}

void CRSF_ResetMspQueue(void)
{
    flush(&MspWriteFIFO);
    CRSF.MspDataLength = 0;
    memset(CRSF.MspData, 0, ELRS_MSP_BUFFER);
}

void CRSF_UnlockMspMessage()
{
    // current msp message is sent so restore next buffered write
    if (size(&MspWriteFIFO) > 0)
    {
        CRSF.MspDataLength = pop(&MspWriteFIFO);
        popBytes(&MspWriteFIFO, CRSF.MspData, CRSF.MspDataLength);
    }
    else
    {
        // no msp message is ready to send currently
        CRSF.MspDataLength = 0;
        memset(CRSF.MspData, 0, ELRS_MSP_BUFFER);
    }
}

void CRSF_AddMspMessage_packet(mspPacket_t* packet, uint8_t destination)
{
    if (packet->payloadSize > ENCAPSULATED_MSP_MAX_PAYLOAD_SIZE)
    {
        return;
    }

    const uint8_t totalBufferLen = packet->payloadSize + ENCAPSULATED_MSP_HEADER_CRC_LEN + CRSF_FRAME_LENGTH_EXT_TYPE_CRC + CRSF_FRAME_NOT_COUNTED_BYTES;
    uint8_t outBuffer[ENCAPSULATED_MSP_MAX_FRAME_LEN + CRSF_FRAME_LENGTH_EXT_TYPE_CRC + CRSF_FRAME_NOT_COUNTED_BYTES];

    // CRSF extended frame header
    outBuffer[0] = CRSF_ADDRESS_BROADCAST;                                      // address
    outBuffer[1] = packet->payloadSize + ENCAPSULATED_MSP_HEADER_CRC_LEN + CRSF_FRAME_LENGTH_EXT_TYPE_CRC; // length
    outBuffer[2] = CRSF_FRAMETYPE_MSP_WRITE;                                    // packet type
    outBuffer[3] = destination;                                                 // destination
    outBuffer[4] = CRSF_ADDRESS_RADIO_TRANSMITTER;                              // origin

    // Encapsulated MSP payload
    outBuffer[5] = 0x30;                // header
    outBuffer[6] = packet->payloadSize; // mspPayloadSize
    outBuffer[7] = packet->function;    // packet->cmd
    for (uint16_t i = 0; i < packet->payloadSize; ++i)
    {
        // copy packet payload into outBuffer
        outBuffer[8 + i] = packet->payload[i];
    }
    // Encapsulated MSP crc
    outBuffer[totalBufferLen - 2] = CalcCRCMsp(&outBuffer[6], packet->payloadSize + 2);

    // CRSF frame crc
    outBuffer[totalBufferLen - 1] = GENERIC_CRC8Calc(&outBuffer[2], packet->payloadSize + ENCAPSULATED_MSP_HEADER_CRC_LEN + CRSF_FRAME_LENGTH_EXT_TYPE_CRC - 1, 0);
    CRSF_AddMspMessage_len(totalBufferLen, outBuffer);
}

void CRSF_AddMspMessage_len(const uint8_t length, uint8_t* data)
{
    if (length > ELRS_MSP_BUFFER)
    {
        return;
    }

    // store next msp message
    if (CRSF.MspDataLength == 0)
    {
        for (uint8_t i = 0; i < length; i++)
        {
            CRSF.MspData[i] = data[i];
        }
        CRSF.MspDataLength = length;
    }
    // store all write requests since an update does send multiple writes
    else
    {
        // MspWriteFIFO.lock();
        if (ensure(&MspWriteFIFO, length + 1))
        {
            push(&MspWriteFIFO, length);
            pushBytes(&MspWriteFIFO, (const uint8_t *)data, length);
        }
        // MspWriteFIFO.unlock();
    }
}

/***
 * @brief: Call this when new uplinkPower from the TX is availble from OTA instead of setting directly
 */
void CRSF_updateUplinkPower(uint8_t uplinkPower)
{
    if (uplinkPower != CRSF.LinkStatistics.crsfLinkStatistics.uplink_TX_Power)
    {
        CRSF.LinkStatistics.crsfLinkStatistics.uplink_TX_Power = uplinkPower;
        CRSF.HasUpdatedUplinkPower = true;
    }
}
/***
 * @brief: Returns true if HasUpdatedUplinkPower and clears the flag
 */
bool CRSF_clearUpdatedUplinkPower(void)
{
    bool retVal = CRSF.HasUpdatedUplinkPower;
    CRSF.HasUpdatedUplinkPower = false;
    return retVal;
}
elrsLinkStatistics_t *CRSF_GetLinkStatistics(void)
{
    return &CRSF.LinkStatistics;
}
