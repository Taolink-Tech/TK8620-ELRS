#pragma once
#include <stdint.h>
#include <stdbool.h>
#define MSP_PORT_INBUF_SIZE 64

#define CHECK_PACKET_PARSING() \
  if (packet->readError) {\
    return;\
  }

typedef enum {
    MSP_PACKET_UNKNOWN,
    MSP_PACKET_COMMAND,
    MSP_PACKET_RESPONSE
} mspPacketType_e;

typedef struct __attribute__((packed)) {
    uint8_t  flags;
    uint16_t function;
    uint16_t payloadSize;
} mspHeaderV2_t;

typedef struct {
    mspPacketType_e type;
    uint8_t         flags;
    uint16_t        function;
    uint16_t        payloadSize;
    uint8_t         payload[MSP_PORT_INBUF_SIZE];
    uint16_t        payloadReadIterator;
    bool            readError;
} mspPacket_t;

static __attribute__((unused)) void MSP_packet_reset(mspPacket_t *packet)
{
    packet->type = MSP_PACKET_UNKNOWN;
    packet->flags = 0;
    packet->function = 0;
    packet->payloadSize = 0;
    packet->payloadReadIterator = 0;
    packet->readError = false;
}

static __attribute__((unused)) void MSP_packet_addByte(mspPacket_t *packet, uint8_t b)
{
    packet->payload[packet->payloadSize++] = b;
}

static __attribute__((unused)) void MSP_packet_makeResponse(mspPacket_t *packet)
{
    packet->type = MSP_PACKET_RESPONSE;
}

static __attribute__((unused)) void MSP_packet_makeCommand(mspPacket_t *packet)
{
    packet->type = MSP_PACKET_COMMAND;
}

static __attribute__((unused)) uint8_t MSP_packet_readByte(mspPacket_t *packet)
{
    if (packet->payloadReadIterator >= packet->payloadSize) {
        // We are trying to read beyond the length of the payload
        packet->readError = true;
        return 0;
    }

    return packet->payload[packet->payloadReadIterator++];
}

