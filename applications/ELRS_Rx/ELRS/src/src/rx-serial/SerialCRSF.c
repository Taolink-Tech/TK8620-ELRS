#include "SerialCRSF.h"
// #include "common.h"
#include "OTA.h"
#include "device.h"
#include "telemetry.h"
// #if defined(USE_MSP_WIFI)
// #include "tcpsocket.h"
// extern TCPSOCKET wifi2tcp;
// #endif
#include "tk86xx_api.h"
#include "crc.h"
#include "crsf_protocol.h"
#include "SerialIO.h"
#include "CRSF.h"

extern Telemetry_t telemetry __attribute__((weak));

extern SerialIO_t serialIO;
// extern void reset_into_bootloader();
extern void UpdateModelMatch(uint8_t model);
void EnterRxBindingModeSafely(void) __attribute__((weak));

static uint16_t map_rssi_dbm_to_uint10(int32_t rssi_dbm, int32_t min_dbm)
{
    const int32_t max_dbm = -50;
    if (min_dbm >= max_dbm)
    {
        return 0;
    }

    rssi_dbm = constrain(rssi_dbm, min_dbm, max_dbm);
    return (uint16_t)(((rssi_dbm - min_dbm) * 1023) / (max_dbm - min_dbm));
}

void SerialCRSF_sendQueuedData(uint32_t maxBytesToSend)
{
    // uint32_t bytesWritten = 0;
    // #if defined(USE_MSP_WIFI)
    // uint8_t OutPktLen;
    // while ((OutPktLen = wifi2tcp.crsfCrsfOutAvailable(maxBytesToSend - bytesWritten)))
    // {
    //     uint8_t OutData[OutPktLen];
    //     wifi2tcp.crsfCrsfOutPop(OutData);
    //     this->_outputPort->write(OutData, OutPktLen); // write the packet out
    //     bytesWritten += OutPktLen;
    // }
    // #endif
    // // Call the super class to send the current FIFO (using any left-over bytes)
    // SerialIO::sendQueuedData(maxBytesToSend - bytesWritten);
}

// void SerialCRSF::queueLinkStatisticsPacket()
// {
//     // Note size of crsfLinkStatistics_t used, not full elrsLinkStatistics_t
//     const uint8_t payloadLen = sizeof(crsfLinkStatistics_t);

//     const uint8_t outBuffer[] = {
//         payloadLen + 4,
//         CRSF_ADDRESS_FLIGHT_CONTROLLER,
//         CRSF_FRAME_SIZE(payloadLen),
//         CRSF_FRAMETYPE_LINK_STATISTICS
//     };

//     uint8_t crc = GENERIC_CRC8CalcByte(outBuffer[3]);
//     crc = GENERIC_CRC8Calc((byte *)CRSF_GetLinkStatistics(), payloadLen, crc);

//     _fifo.lock();
//     if (ensure(&serialIO._fifo, outBuffer[0] + 1))
//     {
//         pushBytes(&serialIO._fifo, outBuffer, sizeof(outBuffer));
//         pushBytes(&serialIO._fifo, (byte *)CRSF_GetLinkStatistics(), payloadLen);
//         push(&serialIO._fifo, crc);
//     }
//     _fifo.unlock();
// }

uint32_t SerialCRSF_SendRCFrame(bool frameAvailable, bool frameMissed, uint32_t *channelData)
{
    if (!frameAvailable)
        return DURATION_IMMEDIATELY;

    crsf_channels_t PackedRCdataOut;
    PackedRCdataOut.ch0 = channelData[0];
    PackedRCdataOut.ch1 = channelData[1];
    PackedRCdataOut.ch2 = channelData[2];
    PackedRCdataOut.ch3 = channelData[3];
    PackedRCdataOut.ch4 = channelData[4];
    PackedRCdataOut.ch5 = channelData[5];
    PackedRCdataOut.ch6 = channelData[6];
    PackedRCdataOut.ch7 = channelData[7];
    PackedRCdataOut.ch8 = channelData[8];
    PackedRCdataOut.ch9 = channelData[9];
    PackedRCdataOut.ch10 = channelData[10];
    PackedRCdataOut.ch11 = channelData[11];
    PackedRCdataOut.ch12 = channelData[12];
    PackedRCdataOut.ch13 = channelData[13];

    // In 16ch mode, do not output RSSI/LQ on channels
    if (OtaIsFullRes && OtaSwitchModeCurrent == smHybridOr16ch)
    {
        PackedRCdataOut.ch14 = channelData[14];
        PackedRCdataOut.ch15 = channelData[15];
    }
    else
    {
        // Not in 16-channel mode, send LQ and RSSI dBm
        elrsLinkStatistics_t *ls = CRSF_GetLinkStatistics();
        int32_t rssiDBM = (ls->crsfLinkStatistics.active_antenna == 0)
            ? -(int32_t)ls->crsfLinkStatistics.uplink_RSSI_1
            : -(int32_t)ls->crsfLinkStatistics.uplink_RSSI_2;

        PackedRCdataOut.ch14 = UINT10_to_CRSF(fmap(ls->crsfLinkStatistics.uplink_Link_quality, 0, 100, 0, 1023));
        PackedRCdataOut.ch15 = UINT10_to_CRSF(map_rssi_dbm_to_uint10(rssiDBM, ExpressLRS_currAirRate_RFperfParams->RXsensitivity));
    }

    const uint8_t outBuffer[] = {
        // No need for length prefix as we aren't using the FIFO
        CRSF_ADDRESS_FLIGHT_CONTROLLER,
        CRSF_FRAME_SIZE(sizeof(PackedRCdataOut)),
        CRSF_FRAMETYPE_RC_CHANNELS_PACKED
    };

    uint8_t crc = GENERIC_CRC8CalcByte(outBuffer[2]);
    crc = GENERIC_CRC8Calc((uint8_t *)&PackedRCdataOut, sizeof(PackedRCdataOut), crc);
    Tk86xxSerialWrite((uint8_t *)outBuffer, sizeof(outBuffer));
    Tk86xxSerialWrite((uint8_t *)&PackedRCdataOut, sizeof(PackedRCdataOut));
    Tk86xxSerialWrite((uint8_t *)&crc, 1);

    return DURATION_IMMEDIATELY;
}

void SerialCRSF_queueMSPFrameTransmission(uint8_t* data)
{
    const uint8_t totalBufferLen = CRSF_FRAME_SIZE(data[1]);
    if (totalBufferLen <= CRSF_FRAME_SIZE_MAX)
    {
        data[0] = CRSF_ADDRESS_FLIGHT_CONTROLLER;
        // _fifo.lock();
        push(&serialIO._fifo, totalBufferLen);
        pushBytes(&serialIO._fifo, data, totalBufferLen);
        // _fifo.unlock();
    }
}

void SerialCRSF_processBytes(uint8_t *bytes, uint16_t size)
{
    for (int i=0 ; i<size ; i++) {
        telemetry.RXhandleUARTin(bytes[i]);

        if (telemetry.ShouldCallBootloader())
        {
            // reset_into_bootloader();
        }
        if (telemetry.ShouldCallEnterBind())
        {
            if (EnterRxBindingModeSafely)
            {
                EnterRxBindingModeSafely();
            }
        }
        if (telemetry.ShouldCallUpdateModelMatch())
        {
            UpdateModelMatch(telemetry.GetUpdatedModelMatch());
        }
        if (telemetry.ShouldSendDeviceFrame())
        {
            uint8_t deviceInformation[DEVICE_INFORMATION_LENGTH];
            CRSF_GetDeviceInformation(deviceInformation, 0);
            CRSF_SetExtendedHeaderAndCrc(deviceInformation, CRSF_FRAMETYPE_DEVICE_INFO, DEVICE_INFORMATION_FRAME_SIZE, CRSF_ADDRESS_CRSF_RECEIVER, CRSF_ADDRESS_FLIGHT_CONTROLLER);
            serialIO.queueMSPFrameTransmission(deviceInformation);
        }
    }
}
