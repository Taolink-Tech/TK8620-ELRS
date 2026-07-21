#include <string.h>
#include "CRSFHandset.h"
#include "CRSF.h"
#include "helpers.h"
#include "device.h"
#include "tk86xx_platform.h"
#include "common.h"
#include "handset.h"
#include "logging.h"
#include "FIFO.h"
#include "config.h"
#include "lua.h"
#include "tk86xx_api.h"

#define CRSF_RX_BUF_SIZE (CRSF_MAX_PACKET_LEN * 2)
CRSFHandset_t CRSFHandset = {
    .maxPeriodBytes = CRSF_MAX_PACKET_LEN,
    .maxPacketBytes = CRSF_MAX_PACKET_LEN,
    .elrsLUAmode = false,
    .BadPktsCountResult = 0,
    .GoodPktsCountResult = 0,
    .ForwardDevicePings = false,
};
static volatile uint16_t s_rx_wr = 0;
static volatile uint16_t s_rx_rd = 0;
static uint8_t sRxDataBuf[CRSF_RX_BUF_SIZE];

#define HANDSET_TELEMETRY_FIFO_SIZE  (128) 

static FIFO_t SerialOutFIFO;

#if !defined(USE_SBUS_PROTOCOL) && !defined(USE_INVERTED_SBUS_PROTOCOL)
/* UART RX interrupt callback: buffer incoming bytes only. */
RAMCODE_SECTION static void Crsf_UartRxCallback(uint8_t *data, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++){
        uint16_t next = (uint16_t)((s_rx_wr + 1) % CRSF_RX_BUF_SIZE);
        if (next == s_rx_rd){
            /* Overflow: drop the oldest byte and keep the latest stream moving. */
            s_rx_rd = (uint16_t)((s_rx_rd + 1) % CRSF_RX_BUF_SIZE);
        }
        sRxDataBuf[s_rx_wr] = data[i];
        s_rx_wr = next;
    }
}
#endif

void CRSFHandset_UartInBufRst(void)
{
    memset(sRxDataBuf, 0, CRSF_RX_BUF_SIZE);
    s_rx_wr = s_rx_rd = 0;
    memset(&CRSFHandset.inBuffer, 0, sizeof(CRSFHandset.inBuffer));
    CRSFHandset.SerialInPacketPtr = 0;
}

#if !defined(USE_SBUS_PROTOCOL) && !defined(USE_INVERTED_SBUS_PROTOCOL)
/* Read bytes from the local ring buffer for packet parsing.
 * Returns the number of bytes copied, up to maxlen. */
RAMCODE_SECTION static uint16_t CRSFHandsetReadBytes(uint8_t *dest, uint16_t maxlen)
{
    uint16_t rd;
    uint16_t wr;
    uint16_t available;
    uint16_t toCopy;
    uint16_t firstChunk;

    if (maxlen == 0 || dest == NULL)
        return 0;

    rd = (uint16_t)s_rx_rd;
    wr = (uint16_t)s_rx_wr;
    if (wr >= rd) {
        available = (uint16_t)(wr - rd);
    } else {
        available = (uint16_t)(CRSF_RX_BUF_SIZE - rd + wr);
    }

    toCopy = (maxlen < available) ? maxlen : available;
    if (toCopy == 0) {
        return 0;
    }

    firstChunk = (toCopy <= (uint16_t)(CRSF_RX_BUF_SIZE - rd)) ? toCopy : (uint16_t)(CRSF_RX_BUF_SIZE - rd);
    if (firstChunk > 0) {
        /* Avoid memcpy() in RAMCODE so the compiler does not pull in helper stubs. */
        volatile uint8_t *d = (volatile uint8_t *)dest;
        const volatile uint8_t *s = (const volatile uint8_t *)&sRxDataBuf[rd];
        uint16_t n = firstChunk;
        while (n--) {
            *d++ = *s++;
        }
    }
    if (toCopy > firstChunk) {
        uint16_t remaining = (uint16_t)(toCopy - firstChunk);
        volatile uint8_t *d = (volatile uint8_t *)(dest + firstChunk);
        const volatile uint8_t *s = (const volatile uint8_t *)sRxDataBuf;
        uint16_t n = remaining;
        while (n--) {
            *d++ = *s++;
        }
    }

    /* Avoid modulo here as well so the RAMCODE path stays lightweight. */
    {
        uint16_t newRd = (uint16_t)(rd + toCopy);
        if (newRd >= (uint16_t)CRSF_RX_BUF_SIZE) {
            newRd = (uint16_t)(newRd - (uint16_t)CRSF_RX_BUF_SIZE);
        }
        s_rx_rd = newRd;
    }
    
    return toCopy;
}

static void CRSFHandset_Begin(void)
{
    s_rx_wr = s_rx_rd = 0;
    Tk86xxSerialRegisterRxCallback(Crsf_UartRxCallback);
    CRSFHandset.halfDuplex = true;
}
#endif

void CRSFHandset_makeLinkStatisticsPacket(uint8_t *buffer)
{
    // Note size of crsfLinkStatistics_t used, not full elrsLinkStatistics_t
    const uint8_t payloadLen = sizeof(crsfLinkStatistics_t);
    // static uint8_t CRSFoutBuffer3[CRSF_MAX_PACKET_LEN] = {0xEA, 0x0C, 0x14, 0xE3, 0x00, 0x64, 0x0C, 0x00, 0x07, 0x03, 0xEA, 0x64, 0x0D, 0x69}; // 14

    buffer[0] = CRSF_ADDRESS_RADIO_TRANSMITTER;
    buffer[1] = CRSF_FRAME_SIZE(payloadLen);
    buffer[2] = CRSF_FRAMETYPE_LINK_STATISTICS;
    memcpy(&buffer[3], (uint8_t *)CRSF_GetLinkStatistics(), payloadLen);
    // memcpy(&buffer[3], (uint8_t *)&CRSFoutBuffer3[3], payloadLen);
    buffer[payloadLen + 3] = GENERIC_CRC8Calc(&buffer[2], payloadLen + 1, 0);
}

/**
 * Build a an extended type packet and queue it in the SerialOutFIFO
 * This is just a regular packet with 2 extra bytes with the sub src and target
 **/
void CRSFHandset_packetQueueExtended(uint8_t type, void *data, uint8_t len)
{
    uint8_t buf[6] = {
        (uint8_t)(len + 6),
        CRSF_ADDRESS_RADIO_TRANSMITTER,
        (uint8_t)(len + 4),
        type,
        CRSF_ADDRESS_RADIO_TRANSMITTER,
        CRSF_ADDRESS_CRSF_TRANSMITTER
    };

    // CRC - Starts at type, ends before CRC
    uint8_t crc = GENERIC_CRC8Calc(&buf[3], sizeof(buf)-3, 0);
    crc = GENERIC_CRC8Calc((uint8_t *)data, len, crc);

    // SerialOutFIFO.lock();
    if (ensure(&SerialOutFIFO, buf[0] + 1))
    {
        pushBytes(&SerialOutFIFO, buf, sizeof(buf));
        pushBytes(&SerialOutFIFO, (uint8_t *)data, len);
        push(&SerialOutFIFO, crc);
    }
    // SerialOutFIFO.unlock();
}

void CRSFHandset_sendTelemetryToTX(uint8_t *data)
{
    if (handset.controllerConnected)
    {
        // DBGLN("sendTelemetryToTX");
        uint8_t packetSize = CRSF_FRAME_SIZE(data[CRSF_TELEMETRY_LENGTH_INDEX]);
        if (packetSize > CRSF_MAX_PACKET_LEN)
        {
            ERRLN("too large");
            return;
        }

        data[0] = CRSF_ADDRESS_RADIO_TRANSMITTER;
        // SerialOutFIFO.lock();
        if (data[CRSF_TELEMETRY_TYPE_INDEX] == CRSF_FRAMETYPE_LINK_STATISTICS && size(&SerialOutFIFO) > 0)
        {
            return;
        }
        if (ensure(&SerialOutFIFO, packetSize + 1))
        {
            push(&SerialOutFIFO, packetSize); // length
            pushBytes(&SerialOutFIFO, data, packetSize);
        }
        // SerialOutFIFO.unlock();
    }
}

// void CRSFHandset::setPacketInterval(int32_t PacketInterval)
// {
//     RequestedRCpacketInterval = PacketInterval;
//     OpenTXsyncOffset = 0;
//     OpenTXsyncWindow = 0;
//     OpenTXsyncWindowSize = std::max((int32_t)1, (int32_t)(20000/RequestedRCpacketInterval));
//     OpenTXsyncLastSent -= OpenTXsyncPacketInterval;
//     adjustMaxPacketSize();
// }

// void CRSFHandset::JustSentRFpacket()
// {
//     // read them in this order to prevent a potential race condition
//     uint32_t last = dataLastRecv;
//     uint32_t m = micros();
//     auto delta = (int32_t)(m - last);

//     if (delta >= (int32_t)RequestedRCpacketInterval)
//     {
//         // missing/late packet, force resync
//         OpenTXsyncOffset = -(delta % RequestedRCpacketInterval) * 10;
//         OpenTXsyncWindow = 0;
//         OpenTXsyncLastSent -= OpenTXsyncPacketInterval;
// #ifdef DEBUG_OPENTX_SYNC
//         DBGLN("Missed packet, forced resync (%d)!", delta);
// #endif
//     }
//     else
//     {
//         // The number of packets in the sync window is how many will fit in 20ms.
//         // This gives quite quite coarse changes for 50Hz, but more fine grained changes at 1000Hz.
//         OpenTXsyncWindow = MIN(OpenTXsyncWindow + 1, (int32_t)OpenTXsyncWindowSize);
//         OpenTXsyncOffset = ((OpenTXsyncOffset * (OpenTXsyncWindow-1)) + delta * 10) / OpenTXsyncWindow;
//     }
// }

// void CRSFHandset::sendSyncPacketToTX() // in values in us.
// {
//     uint32_t now = millis();
//     if (controllerConnected && (now - OpenTXsyncLastSent) >= OpenTXsyncPacketInterval)
//     {
//         int32_t packetRate = RequestedRCpacketInterval * 10; //convert from us to right format
//         int32_t offset = OpenTXsyncOffset - OpenTXsyncOffsetSafeMargin; // offset so that opentx always has some headroom
// #ifdef DEBUG_OPENTX_SYNC
//         DBGLN("Offset %d", offset); // in 10ths of us (OpenTX sync unit)
// #endif

//         struct otxSyncData {
//             uint8_t subType; // CRSF_HANDSET_SUBCMD_TIMING
//             uint32_t rate; // Big-Endian
//             uint32_t offset; // Big-Endian
//         } PACKED;

//         uint8_t buffer[sizeof(otxSyncData)];
//         auto * const sync = (struct otxSyncData * const)buffer;

//         sync->subType = CRSF_HANDSET_SUBCMD_TIMING;
//         sync->rate = htobe32(packetRate);
//         sync->offset = htobe32(offset);

//         CRSFHandset.packetQueueExtended(CRSF_FRAMETYPE_HANDSET, buffer, sizeof(buffer));

//         OpenTXsyncLastSent = now;
//     }
// }

void CRSFHandsetRcPacketToChannelsData(void) // data is packed as 11 bits per channel
{
    const uint8_t * const payload = (uint8_t *)&CRSFHandset.inBuffer.asRCPacket_t.channels;
    const unsigned srcBits = 11;
    const unsigned dstBits = 11;
    const unsigned inputChannelMask = (1 << srcBits) - 1;
    const unsigned precisionShift = dstBits - srcBits;

    // code from BetaFlight rx/crsf.cpp / bitpacker_unpack
    uint8_t bitsMerged = 0;
    uint32_t readValue = 0;
    unsigned readByteIndex = 0;
    for (size_t i = 0; i < ARRAY_SIZE(ChannelData); i++) {
        while (bitsMerged < srcBits)
        {
            uint8_t readByte = payload[readByteIndex++];
            readValue |= ((uint32_t) readByte) << bitsMerged;
            bitsMerged += 8;
        }
        //printf("rv=%x(%x) bm=%u\n", readValue, (readValue & inputChannelMask), bitsMerged);
        ChannelData[i] = (readValue & inputChannelMask) << precisionShift;
        readValue >>= srcBits;
        bitsMerged -= srcBits;
    }
}

#if !defined(USE_SBUS_PROTOCOL) && !defined(USE_INVERTED_SBUS_PROTOCOL)
static bool CRSFHandsetIsAllowedModelSelectDest(uint8_t dest_addr)
{
    return dest_addr == CRSF_ADDRESS_CRSF_TRANSMITTER ||
        dest_addr == CRSF_ADDRESS_BROADCAST ||
        dest_addr == CRSF_ADDRESS_CRSF_RECEIVER;
}

static bool CRSFHandsetIsAllowedRadioOrigin(uint8_t orig_addr)
{
    return orig_addr == CRSF_ADDRESS_RADIO_TRANSMITTER ||
        orig_addr == CRSF_ADDRESS_ELRS_LUA;
}

static bool CRSFHandsetIsModelSelectCommand(const crsf_ext_header_t *header)
{
    return header->type == CRSF_FRAMETYPE_COMMAND &&
        header->frame_size >= 7 &&
        CRSFHandsetIsAllowedModelSelectDest(header->dest_addr) &&
        CRSFHandsetIsAllowedRadioOrigin(header->orig_addr) &&
        header->payload[0] == CRSF_COMMAND_SUBCMD_RX &&
        header->payload[1] == CRSF_COMMAND_MODEL_SELECT_ID;
}

static void CRSFHandsetProcessModelSelect(const crsf_ext_header_t *header)
{
    const uint8_t newModelId = header->payload[2];
    if (newModelId < CONFIG_TX_MODEL_CNT)
    {
        txConfig.m_modelId = newModelId;
        if (CRSFHandset.handset != NULL && CRSFHandset.handset->RecvModelUpdate) CRSFHandset.handset->RecvModelUpdate();
    }
}

static bool CRSFHandsetProcessInternalCrsfPackage(uint8_t *package)
{
    const crsf_ext_header_t *header = (crsf_ext_header_t *)package;
    const crsf_frame_type_e packetType = (crsf_frame_type_e)header->type;
    const bool hasCommandValue = header->frame_size >= 7;

    // Enter Binding Mode
    if (packetType == CRSF_FRAMETYPE_COMMAND
        && header->frame_size >= 6 // official CRSF is 7 bytes with two CRCs
        && header->dest_addr == CRSF_ADDRESS_CRSF_TRANSMITTER
        && header->orig_addr == CRSF_ADDRESS_RADIO_TRANSMITTER
        && header->payload[0] == CRSF_COMMAND_SUBCMD_RX
        && header->payload[1] == CRSF_COMMAND_SUBCMD_RX_BIND)
    {
        if (CRSFHandset.handset != NULL && CRSFHandset.handset->OnBindingCommand) CRSFHandset.handset->OnBindingCommand();
        return true;
    }

    if (CRSFHandsetIsModelSelectCommand(header))
    {
        CRSFHandset.elrsLUAmode = header->orig_addr == CRSF_ADDRESS_ELRS_LUA;
        CRSFHandsetProcessModelSelect(header);
        return true;
    }

    if (packetType >= CRSF_FRAMETYPE_DEVICE_PING &&
        (header->dest_addr == CRSF_ADDRESS_CRSF_TRANSMITTER || header->dest_addr == CRSF_ADDRESS_BROADCAST) &&
        (header->orig_addr == CRSF_ADDRESS_RADIO_TRANSMITTER || header->orig_addr == CRSF_ADDRESS_ELRS_LUA))
    {
        CRSFHandset.elrsLUAmode = header->orig_addr == CRSF_ADDRESS_ELRS_LUA;

        if (packetType == CRSF_FRAMETYPE_COMMAND && hasCommandValue && header->payload[0] == CRSF_COMMAND_SUBCMD_RX && header->payload[1] == CRSF_COMMAND_MODEL_SELECT_ID)
        {
            CRSFHandsetProcessModelSelect(header);
        }
        else if (packetType == CRSF_FRAMETYPE_DEVICE_PING)
        {
            sendLuaDevicePacket();
        }
        else
        {
            if (CRSFHandset.handset->RecvParameterUpdate)
            {
                CRSFHandset.handset->RecvParameterUpdate(packetType, header->payload[0], header->payload[1]);
            }
            else
            {
                luaParamUpdateReq(packetType, header->payload[0], header->payload[1]);
            }
        }

        return true;
    }

    return false;
}

static bool CRSFHandsetProcessPacket()
{
    bool packetReceived = false;

    CRSFHandset.dataLastRecv = micros();

    if (!CRSFHandset.handset->controllerConnected)
    {
        CRSFHandset.handset->controllerConnected = true;
        DBGLN("CRSF UART Connected");
        if (CRSFHandset.handset->connected) CRSFHandset.handset->connected();
    }

    const uint8_t packetType = CRSFHandset.inBuffer.asRCPacket_t.header.type;
    uint8_t *SerialInBuffer = CRSFHandset.inBuffer.asUint8_t;

    if (packetType == CRSF_FRAMETYPE_RC_CHANNELS_PACKED)
    {
        CRSFHandset.handset->RCdataLastRecv = micros();
        CRSFHandsetRcPacketToChannelsData();
        packetReceived = true;
    }
    // check for all extended frames that are a broadcast or a message to the FC
    else if (packetType >= CRSF_FRAMETYPE_DEVICE_PING &&
            (SerialInBuffer[3] == CRSF_ADDRESS_FLIGHT_CONTROLLER || SerialInBuffer[3] == CRSF_ADDRESS_BROADCAST || SerialInBuffer[3] == CRSF_ADDRESS_CRSF_RECEIVER))
    {
        const bool internalModelSelect = CRSFHandsetIsModelSelectCommand((const crsf_ext_header_t *)SerialInBuffer);
        // Some types trigger telemburst to attempt a connection even with telm off
        // but for pings (which are sent when the user loads Lua) do not forward
        // unless connected
        if (!internalModelSelect && (CRSFHandset.ForwardDevicePings || packetType != CRSF_FRAMETYPE_DEVICE_PING))
        {
            const uint8_t length = CRSFHandset.inBuffer.asRCPacket_t.header.frame_size + 2;
            CRSF_AddMspMessage_len(length, SerialInBuffer);
        }
        packetReceived = true;
    }

    packetReceived |= CRSFHandsetProcessInternalCrsfPackage(SerialInBuffer);

    return packetReceived;
}

static void CRSFHandsetAlignBufferToSync(uint8_t startIdx)
{
    uint8_t *SerialInBuffer = CRSFHandset.inBuffer.asUint8_t;

    for (unsigned int i=startIdx ; i<CRSFHandset.SerialInPacketPtr ; i++)
    {
        // If we find a header byte then move that and trailing bytes to the head of the buffer and let's go!
        if (SerialInBuffer[i] == CRSF_ADDRESS_CRSF_TRANSMITTER || SerialInBuffer[i] == CRSF_SYNC_BYTE)
        {
            CRSFHandset.SerialInPacketPtr -= i;
            memmove(SerialInBuffer, &SerialInBuffer[i], CRSFHandset.SerialInPacketPtr);
            return;
        }
    }

    // If no header found then discard this entire buffer
    CRSFHandset.SerialInPacketPtr = 0;
}

static void handleOutput(int receivedBytes)
{
    static uint8_t CRSFoutBuffer[CRSF_MAX_PACKET_LEN] = {0};
    // both static to split up larger packages
    static uint8_t packageLengthRemaining = 0;
    static uint8_t sendingOffset = 0;

    if (!CRSFHandset.handset->controllerConnected)
    {
        // SerialOutFIFO.lock();
        flush(&SerialOutFIFO);
        // SerialOutFIFO.unlock();
        return;
    }

    if (packageLengthRemaining == 0 && size(&SerialOutFIFO) == 0)
    {
        // sendSyncPacketToTX(); // calculate mixer sync packet if needed
    }

    // if partial package remaining, or data in the output FIFO that needs to be written
    if (packageLengthRemaining > 0 || size(&SerialOutFIFO) > 0) {
        uint8_t periodBytesRemaining = HANDSET_TELEMETRY_FIFO_SIZE;
        if (CRSFHandset.halfDuplex)
        {
            periodBytesRemaining = MIN((CRSFHandset.maxPeriodBytes - 0 % CRSFHandset.maxPeriodBytes), (int)CRSFHandset.maxPacketBytes); // receivedBytes -> 0
            periodBytesRemaining = MAX(periodBytesRemaining, (uint8_t)10);
            // DBGLN("periodBytesRemaining: %d", periodBytesRemaining);
            // if (!transmitting)
            // {
            //     transmitting = true;
            //     duplex_set_TX();
            // }
        }

        do
        {
            // SerialOutFIFO.lock();
            // no package is in transit so get new data from the fifo
            if (packageLengthRemaining == 0)
            {
                packageLengthRemaining = pop(&SerialOutFIFO);
                popBytes(&SerialOutFIFO, CRSFoutBuffer, packageLengthRemaining);
                sendingOffset = 0;
            }
            // SerialOutFIFO.unlock();

            // if the package is long we need to split it, so it fits in the sending interval
            uint8_t writeLength = MIN(packageLengthRemaining, periodBytesRemaining);

            // write the packet out, if it's a large package the offset holds the starting position
            // CRSFHandset::Port.write(CRSFoutBuffer + sendingOffset, writeLength);
            Tk86xxSerialWrite(CRSFoutBuffer + sendingOffset, writeLength);
            // DBGLN("out to Handset");
            // if (CRSFHandset::PortSecondary)
            // {
            //     CRSFHandset::PortSecondary->write(CRSFoutBuffer + sendingOffset, writeLength);
            // }

            sendingOffset += writeLength;
            packageLengthRemaining -= writeLength;
            periodBytesRemaining -= writeLength;
        } while(periodBytesRemaining != 0 && size(&SerialOutFIFO) != 0);
    }
}

void CRSFHandset_FlushOutput(void)
{
    handleOutput(0);
}

static bool CRSFHandset_handleInput(void)
{
    // DBGLN("CRSFHandset_handleInput");
    uint8_t *SerialInBuffer = CRSFHandset.inBuffer.asUint8_t;
    bool packetReceived = false;

    // Add new data, and then discard bytes until we start with header byte
    volatile uint8_t toRead = CRSF_MAX_PACKET_LEN - CRSFHandset.SerialInPacketPtr;
    CRSFHandset.SerialInPacketPtr += (uint8_t)CRSFHandsetReadBytes(&SerialInBuffer[CRSFHandset.SerialInPacketPtr], toRead);
    CRSFHandsetAlignBufferToSync(0);

    // Make sure we have at least a packet header and a length byte
    if (CRSFHandset.SerialInPacketPtr < 3) 
        return false;

    // Sanity check: A total packet must be at least [sync][len][type][crc] (if no payload) and at most CRSF_MAX_PACKET_LEN
    const uint32_t totalLen = SerialInBuffer[1] + 2;
    if (totalLen < 4 || totalLen > CRSF_MAX_PACKET_LEN)
    {
        // Start looking for another packet after this start byte
        CRSFHandsetAlignBufferToSync(1);
        return false;
    }

    // Only proceed one there are enough bytes in the buffer for the entire packet
    if (CRSFHandset.SerialInPacketPtr < totalLen) 
        return false;

    uint8_t CalculatedCRC = GENERIC_CRC8Calc(&SerialInBuffer[2], totalLen - 3, 0);
    if (CalculatedCRC == SerialInBuffer[totalLen - 1])
    {
        CRSFHandset.GoodPktsCount++;
        if (CRSFHandsetProcessPacket())
        {
            packetReceived = true;
            handleOutput(totalLen);
            if (handset.RCdataCallback)
            {
                handset.RCdataCallback();
            }
        }
    }
    else
    {
        // DBGLN("UART CRC failure, CalculatedCRC=0x%02X, ExpectedCRC=0x%02X", CalculatedCRC, SerialInBuffer[totalLen - 1]);
        DBG("c");
        CRSFHandset.BadPktsCount++;
    }

    CRSFHandset.SerialInPacketPtr -= totalLen;
    memmove(SerialInBuffer, &SerialInBuffer[totalLen], CRSFHandset.SerialInPacketPtr);

    return packetReceived;
}
#endif

// void CRSFHandset::duplex_set_RX() const
// {
// #if defined(PLATFORM_ESP32)
//     ESP_ERROR_CHECK(gpio_set_direction((gpio_num_t)GPIO_PIN_RCSIGNAL_RX, GPIO_MODE_INPUT));
//     if (UARTinverted)
//     {
//         gpio_matrix_in((gpio_num_t)GPIO_PIN_RCSIGNAL_RX, U0RXD_IN_IDX, true);
//         gpio_pulldown_en((gpio_num_t)GPIO_PIN_RCSIGNAL_RX);
//         gpio_pullup_dis((gpio_num_t)GPIO_PIN_RCSIGNAL_RX);
//     }
//     else
//     {
//         gpio_matrix_in((gpio_num_t)GPIO_PIN_RCSIGNAL_RX, U0RXD_IN_IDX, false);
//         gpio_pullup_en((gpio_num_t)GPIO_PIN_RCSIGNAL_RX);
//         gpio_pulldown_dis((gpio_num_t)GPIO_PIN_RCSIGNAL_RX);
//     }
// #elif defined(PLATFORM_ESP8266)
//     // Enable loopback on UART0 to connect the RX pin to the TX pin (not done, connection is full duplex uninverted)
//     //USC0(UART0) |= BIT(UCLBE);
// #elif defined(GPIO_PIN_BUFFER_OE) && (GPIO_PIN_BUFFER_OE != UNDEF_PIN)
//     digitalWrite(GPIO_PIN_BUFFER_OE, LOW ^ GPIO_PIN_BUFFER_OE_INVERTED);
// #elif (GPIO_PIN_RCSIGNAL_TX == GPIO_PIN_RCSIGNAL_RX)
//     CRSFHandset::Port.enableHalfDuplexRx();
// #endif
// }

// void CRSFHandset::duplex_set_TX() const
// {
// #if defined(PLATFORM_ESP32)
//     ESP_ERROR_CHECK(gpio_set_pull_mode((gpio_num_t)GPIO_PIN_RCSIGNAL_TX, GPIO_FLOATING));
//     ESP_ERROR_CHECK(gpio_set_pull_mode((gpio_num_t)GPIO_PIN_RCSIGNAL_RX, GPIO_FLOATING));
//     if (UARTinverted)
//     {
//         ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)GPIO_PIN_RCSIGNAL_TX, 0));
//         ESP_ERROR_CHECK(gpio_set_direction((gpio_num_t)GPIO_PIN_RCSIGNAL_TX, GPIO_MODE_OUTPUT));
//         const uint8_t MATRIX_DETACH_IN_LOW = 0x30; // routes 0 to matrix slot
//         gpio_matrix_in(MATRIX_DETACH_IN_LOW, U0RXD_IN_IDX, false); // Disconnect RX from all pads
//         gpio_matrix_out((gpio_num_t)GPIO_PIN_RCSIGNAL_TX, U0TXD_OUT_IDX, true, false);
//     }
//     else
//     {
//         ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)GPIO_PIN_RCSIGNAL_TX, 1));
//         ESP_ERROR_CHECK(gpio_set_direction((gpio_num_t)GPIO_PIN_RCSIGNAL_TX, GPIO_MODE_OUTPUT));
//         const uint8_t MATRIX_DETACH_IN_HIGH = 0x38; // routes 1 to matrix slot
//         gpio_matrix_in(MATRIX_DETACH_IN_HIGH, U0RXD_IN_IDX, false); // Disconnect RX from all pads
//         gpio_matrix_out((gpio_num_t)GPIO_PIN_RCSIGNAL_TX, U0TXD_OUT_IDX, false, false);
//     }
// #elif defined(PLATFORM_ESP8266)
//     // Disable loopback to disconnect the RX pin from the TX pin (not done, connection is full duplex uninverted)
//     //USC0(UART0) &= ~BIT(UCLBE);
// #elif defined(GPIO_PIN_BUFFER_OE) && (GPIO_PIN_BUFFER_OE != UNDEF_PIN)
//     digitalWrite(GPIO_PIN_BUFFER_OE, HIGH ^ GPIO_PIN_BUFFER_OE_INVERTED);
// #elif (GPIO_PIN_RCSIGNAL_TX == GPIO_PIN_RCSIGNAL_RX)
//     // writing to the port switches the mode
// #endif
// }

// int CRSFHandset::getMinPacketInterval() const
// {
//     if (CRSFHandset.halfDuplex && CRSFHandset::GetCurrentBaudRate() == 115200) // Packet rate limited to 200Hz if we are on 115k baud on half-duplex module
//     {
//         return 5000;
//     }
//     else if (CRSFHandset::GetCurrentBaudRate() == 115200) // Packet rate limited to 250Hz if we are on 115k baud
//     {
//         return 4000;
//     }
//     else if (CRSFHandset::GetCurrentBaudRate() == 400000) // Packet rate limited to 500Hz if we are on 400k baud
//     {
//         return 2000;
//     }
//     return 1;   // 1-million Hz!
// }

// void CRSFHandset::adjustMaxPacketSize()
// {
//     const int LUA_CHUNK_QUERY_SIZE = 26;
//     // The number of bytes that fit into a CRSF window : baud / 10bits-per-byte / rate(Hz) * 87% (for some leeway)
//     // 87% was arrived at by measuring the time taken for a chunk query packet and the processing times and switching times
//     // involved from RX -> TX and vice-versa. The maxPacketBytes is used as the Lua chunk size so each chunk can be returned
//     // to the handset and not be broken across time-slots as there can be issues with spurious glitches on the s.port pin
//     // which switching direction. It also appears that the absolute minimum packet size should be 15 bytes as this will fit
//     // the LinkStatistics and OpenTX sync packets.
//     maxPeriodBytes = MIN((int)(UARTrequestedBaud / 10 / (1000000/RequestedRCpacketInterval) * 87 / 100), HANDSET_TELEMETRY_FIFO_SIZE);
//     // Maximum number of bytes we can send in a single window, half the period bytes, upto one full CRSF packet.
//     maxPacketBytes = MIN(maxPeriodBytes - max(maxPeriodBytes / 2, LUA_CHUNK_QUERY_SIZE), CRSF_MAX_PACKET_LEN);
//     DBGLN("Adjusted max packet size %u-%u", maxPacketBytes, maxPeriodBytes);
// }

// #if defined(PLATFORM_ESP32_S3)
// uint32_t CRSFHandset::autobaud()
// {
//     static enum { INIT, MEASURED, INVERTED } state;

//     if (state == MEASURED)
//     {
//         UARTinverted = !UARTinverted;
//         state = INVERTED;
//         return UARTrequestedBaud;
//     }
//     if (state == INVERTED)
//     {
//         UARTinverted = !UARTinverted;
//         state = INIT;
//     }

//     if (REG_GET_BIT(UART_CONF0_REG(0), UART_AUTOBAUD_EN) == 0)
//     {
//         REG_WRITE(UART_RX_FILT_REG(0), (4 << UART_GLITCH_FILT_S) | UART_GLITCH_FILT_EN); // enable, glitch filter 4
//         REG_WRITE(UART_LOWPULSE_REG(0), 4095); // reset register to max value
//         REG_WRITE(UART_HIGHPULSE_REG(0), 4095); // reset register to max value
//         REG_SET_BIT(UART_CONF0_REG(0), UART_AUTOBAUD_EN); // enable autobaud
//         return 400000;
//     }
//     if (REG_GET_BIT(UART_CONF0_REG(0), UART_AUTOBAUD_EN) && REG_READ(UART_RXD_CNT_REG(0)) < 300)
//     {
//         return 400000;
//     }

//     state = MEASURED;

//     const uint32_t low_period  = REG_READ(UART_LOWPULSE_REG(0));
//     const uint32_t high_period = REG_READ(UART_HIGHPULSE_REG(0));
//     REG_CLR_BIT(UART_CONF0_REG(0), UART_AUTOBAUD_EN); // disable autobaud
//     REG_CLR_BIT(UART_RX_FILT_REG(0), UART_GLITCH_FILT_EN); // disable glitch filtering

//     DBGLN("autobaud: low %d, high %d", low_period, high_period);
//     // According to the tecnnical reference
//     const int32_t calulatedBaud = UART_CLK_FREQ / (low_period + high_period + 2);
//     int32_t bestBaud = TxToHandsetBauds[0];
//     for(int i=0 ; i<ARRAY_SIZE(TxToHandsetBauds) ; i++)
//     {
//         if (abs(calulatedBaud - bestBaud) > abs(calulatedBaud - TxToHandsetBauds[i]))
//         {
//             bestBaud = TxToHandsetBauds[i];
//         }
//     }
//     return bestBaud;
// }
// #elif defined(PLATFORM_ESP32)
// uint32_t CRSFHandset::autobaud()
// {
//     static enum { INIT, MEASURED, INVERTED } state;

//     if (state == MEASURED) {
//         UARTinverted = !UARTinverted;
//         state = INVERTED;
//         return UARTrequestedBaud;
//     }
//     if (state == INVERTED) {
//         UARTinverted = !UARTinverted;
//         state = INIT;
//     }

//     if (REG_GET_BIT(UART_AUTOBAUD_REG(0), UART_AUTOBAUD_EN) == 0) {
//         REG_WRITE(UART_AUTOBAUD_REG(0), 4 << UART_GLITCH_FILT_S | UART_AUTOBAUD_EN);    // enable, glitch filter 4
//         return 400000;
//     }
//     if (REG_GET_BIT(UART_AUTOBAUD_REG(0), UART_AUTOBAUD_EN) && REG_READ(UART_RXD_CNT_REG(0)) < 300)
//     {
//         return 400000;
//     }

//     state = MEASURED;

//     auto low_period  = (int32_t)REG_READ(UART_LOWPULSE_REG(0));
//     auto high_period = (int32_t)REG_READ(UART_HIGHPULSE_REG(0));
//     REG_CLR_BIT(UART_AUTOBAUD_REG(0), UART_AUTOBAUD_EN);   // disable autobaud

//     DBGLN("autobaud: low %d, high %d", low_period, high_period);
//     // sample code at https://github.com/espressif/esp-idf/issues/3336
//     // says baud rate = 80000000/min(UART_LOWPULSE_REG, UART_HIGHPULSE_REG);
//     // Based on testing use max and add 2 for lowest deviation
//     int32_t calculatedBaud = 80000000 / (max(low_period, high_period) + 3);
//     auto bestBaud = TxToHandsetBauds[0];
//     for(int TxToHandsetBaud : TxToHandsetBauds)
//     {
//         if (abs(calculatedBaud - bestBaud) > abs(calculatedBaud - (int32_t)TxToHandsetBaud))
//         {
//             bestBaud = (int32_t)TxToHandsetBaud;
//         }
//     }
//     return bestBaud;
// }
// #else
// uint32_t CRSFHandset::autobaud() {
//     UARTcurrentBaudIdx = (UARTcurrentBaudIdx + 1) % ARRAY_SIZE(TxToHandsetBauds);
//     return TxToHandsetBauds[UARTcurrentBaudIdx];
// }
#if !defined(USE_SBUS_PROTOCOL) && !defined(USE_INVERTED_SBUS_PROTOCOL)
static uint8_t CRSFHandset_GetMaxPacketBytes()
{ 
    return CRSFHandset.maxPacketBytes; 
}
#endif

void CRSFHandset_registerParameterUpdateCallback(void (*callback)(uint8_t type, uint8_t index, uint8_t arg)) 
{ 
    CRSFHandset.handset->RecvParameterUpdate = callback; 
}

#if !defined(USE_SBUS_PROTOCOL) && !defined(USE_INVERTED_SBUS_PROTOCOL)
static uint8_t CRSFHandset_GetModelID() 
{ 
    return txConfig.m_modelId; 
}

static bool CRSFHandset_IsArmed(void) 
{ 
    return CRSF_to_BIT(ChannelData[4]); 
}
#endif

void CRSFHandset_registerCallbacks(void (*connectedCallback)(), void (*disconnectedCallback)(), void (*RecvModelUpdateCallback)(), void (*bindingCommandCallback)())
{
    CRSFHandset.handset->connected = connectedCallback;
    CRSFHandset.handset->disconnected = disconnectedCallback;
    CRSFHandset.handset->RecvModelUpdate = RecvModelUpdateCallback;
    CRSFHandset.handset->OnBindingCommand = bindingCommandCallback;
}

#if !defined(USE_SBUS_PROTOCOL) && !defined(USE_INVERTED_SBUS_PROTOCOL)
void Handset_Init(Handset_t *handset)
{
    CRSFHandset.handset = handset;
    CRSFHandset.GetMaxPacketBytes = CRSFHandset_GetMaxPacketBytes;
    CRSFHandset.packetQueueExtended = CRSFHandset_packetQueueExtended;
    CRSFHandset.getModelID = CRSFHandset_GetModelID;

    handset->Begin = CRSFHandset_Begin;
    handset->handleInput = CRSFHandset_handleInput;
    handset->sendTelemetryToTX = CRSFHandset_sendTelemetryToTX;
    handset->registerParameterUpdateCallback = CRSFHandset_registerParameterUpdateCallback;
    handset->IsArmed = CRSFHandset_IsArmed;
    handset->registerCallbacks = CRSFHandset_registerCallbacks;
}
#endif
