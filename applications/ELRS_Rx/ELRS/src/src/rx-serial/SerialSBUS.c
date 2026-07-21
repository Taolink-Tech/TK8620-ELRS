#include "SerialSBUS.h"

#include <stddef.h>

#include "device.h"
#include "common.h"
#include "config.h"
#include "SerialIO.h"
#include "tk86xx_api.h"
#include "crsf_protocol.h"

extern SerialIO_t serialIO;

#define SBUS_FRAME_LEN 25
#define SBUS_HEADER 0x0F
#define SBUS_FOOTER 0x00
#define SBUS_PAYLOAD_LEN 22

#define SBUS_FLAG_CH17            (1u << 0)
#define SBUS_FLAG_CH18            (1u << 1)
#define SBUS_FLAG_SIGNAL_LOSS     (1u << 2)
#define SBUS_FLAG_FAILSAFE_ACTIVE (1u << 3)

static const uint32_t UNCONNECTED_CALLBACK_INTERVAL_MS = 10;
static const uint32_t SBUS_CALLBACK_INTERVAL_MS = 9;


uint32_t SerialSBUS_SendRCFrame(bool frameAvailable, bool frameMissed, uint32_t *channelData)
{
    static bool sendPackets = false;

    bool effectivelyFailsafed = serialIO.failsafe || (!connectionHasModelMatch) || (!teamraceHasModelMatch);
    enum eFailsafeMode failsafeMode = FAILSAFE_LAST_POSITION;
    if (rxConfig.GetFailsafeMode)
    {
        failsafeMode = rxConfig.GetFailsafeMode();
    }

    if ((effectivelyFailsafed && failsafeMode == FAILSAFE_NO_PULSES) || (!sendPackets && connectionState != connected) || channelData == NULL)
    {
        return (uint32_t)UNCONNECTED_CALLBACK_INTERVAL_MS;
    }
    sendPackets = true;

    if ((!frameAvailable && !frameMissed && !effectivelyFailsafed))
    {
        return (uint32_t)DURATION_IMMEDIATELY;
    }

    uint8_t flags = 0;
    flags |= frameMissed ? SBUS_FLAG_SIGNAL_LOSS : 0;
    flags |= effectivelyFailsafed ? SBUS_FLAG_FAILSAFE_ACTIVE : 0;

    uint8_t frame[SBUS_FRAME_LEN];
    crsf_channels_t PackedRCdataOut;

    PackedRCdataOut.ch0  = channelData[0];
    PackedRCdataOut.ch1  = channelData[1];
    PackedRCdataOut.ch2  = channelData[2];
    PackedRCdataOut.ch3  = channelData[3];
    PackedRCdataOut.ch4  = channelData[4];
    PackedRCdataOut.ch5  = channelData[5];
    PackedRCdataOut.ch6  = channelData[6];
    PackedRCdataOut.ch7  = channelData[7];
    PackedRCdataOut.ch8  = channelData[8];
    PackedRCdataOut.ch9  = channelData[9];
    PackedRCdataOut.ch10 = channelData[10];
    PackedRCdataOut.ch11 = channelData[11];
    PackedRCdataOut.ch12 = channelData[12];
    PackedRCdataOut.ch13 = channelData[13];
    PackedRCdataOut.ch14 = channelData[14];
    PackedRCdataOut.ch15 = channelData[15];

    frame[0] = SBUS_HEADER;
    const uint8_t *p = (const uint8_t *)&PackedRCdataOut;
    for (uint32_t i = 0; i < (uint32_t)SBUS_PAYLOAD_LEN; i++)
    {
        frame[1 + i] = p[i];
    }
    frame[23] = flags;
    frame[24] = SBUS_FOOTER;

    Tk86xxSerialWrite(frame, SBUS_FRAME_LEN);

    return (uint32_t)SBUS_CALLBACK_INTERVAL_MS;
}

void SerialSBUS_processBytes(uint8_t *bytes, uint16_t size)
{
    (void)bytes;
    (void)size;
}

void SerialSBUS_queueMSPFrameTransmission(uint8_t *data)
{
    (void)data;
}

void SerialSBUS_sendQueuedData(uint32_t maxBytesToSend)
{
    (void)maxBytesToSend;
}
