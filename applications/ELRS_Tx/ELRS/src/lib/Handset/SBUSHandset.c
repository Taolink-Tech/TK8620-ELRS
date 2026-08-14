#include "handset.h"

#if defined(USE_SBUS_PROTOCOL) || defined(USE_INVERTED_SBUS_PROTOCOL)

#include <string.h>

#include "common.h"
#include "config.h"
#include "crsf_protocol.h"
#include "helpers.h"
#include "logging.h"
#include "tk86xx_platform.h"
#include "tk86xx_api.h"
#include "unified_config.h"

#define SBUS_FRAME_LEN     25u
#define SBUS_HEADER        0x0Fu
#define SBUS_PAYLOAD_LEN   22u

#define SBUS_FLAG_CH17            (1u << 0)
#define SBUS_FLAG_CH18            (1u << 1)
#define SBUS_FLAG_SIGNAL_LOSS     (1u << 2)
#define SBUS_FLAG_FAILSAFE_ACTIVE (1u << 3)

// SBUS is typically 100000 8E2. Electrical inversion (if required) is handled in hardware.

#define SBUS_RX_BUF_SIZE 128u
static volatile uint16_t s_rx_wr = 0;
static volatile uint16_t s_rx_rd = 0;
static uint8_t sRxDataBuf[SBUS_RX_BUF_SIZE];

static uint8_t sParseBuf[SBUS_FRAME_LEN * 2u];
static uint8_t sParseLen = 0;

static uint32_t sLastGoodFrameUs = 0;

static void SbusBufferByte(uint8_t byte)
{
    uint16_t next = (uint16_t)(s_rx_wr + 1u);
    if (next >= (uint16_t)SBUS_RX_BUF_SIZE) next = 0;
    if (next == s_rx_rd) {
        uint16_t rd = (uint16_t)(s_rx_rd + 1u);
        if (rd >= (uint16_t)SBUS_RX_BUF_SIZE) rd = 0;
        s_rx_rd = rd;
    }
    sRxDataBuf[s_rx_wr] = byte;
    s_rx_wr = next;
}

RAMCODE_SECTION static void Sbus_UartRxCallback(uint8_t *data, uint8_t len)
{
#if ELRS_UNIFIED
    UnifiedConfig_FilterBytes(data, len, SbusBufferByte);
#else
    for (uint8_t i = 0; i < len; i++) {
        SbusBufferByte(data[i]);
    }
#endif
}

RAMCODE_SECTION static uint16_t SbusReadBytes(uint8_t *dest, uint16_t maxlen)
{
    if (maxlen == 0 || dest == NULL) return 0;

    uint16_t rd = (uint16_t)s_rx_rd;
    uint16_t wr = (uint16_t)s_rx_wr;
    uint16_t available = (wr >= rd) ? (uint16_t)(wr - rd) : (uint16_t)(SBUS_RX_BUF_SIZE - rd + wr);
    uint16_t toCopy = (maxlen < available) ? maxlen : available;
    if (toCopy == 0) return 0;

    uint16_t firstChunk = (toCopy <= (uint16_t)(SBUS_RX_BUF_SIZE - rd)) ? toCopy : (uint16_t)(SBUS_RX_BUF_SIZE - rd);
    for (uint16_t i = 0; i < firstChunk; i++) {
        dest[i] = sRxDataBuf[(uint16_t)(rd + i)];
    }
    if (toCopy > firstChunk) {
        uint16_t remaining = (uint16_t)(toCopy - firstChunk);
        for (uint16_t i = 0; i < remaining; i++) {
            dest[(uint16_t)(firstChunk + i)] = sRxDataBuf[i];
        }
    }

    uint16_t newRd = (uint16_t)(rd + toCopy);
    if (newRd >= (uint16_t)SBUS_RX_BUF_SIZE) newRd = (uint16_t)(newRd - (uint16_t)SBUS_RX_BUF_SIZE);
    s_rx_rd = newRd;

    return toCopy;
}

static void SbusAlignToHeader(uint8_t startIdx)
{
    for (uint8_t i = startIdx; i < sParseLen; i++) {
        if (sParseBuf[i] == SBUS_HEADER) {
            if (i != 0) {
                memmove(sParseBuf, &sParseBuf[i], (size_t)(sParseLen - i));
                sParseLen = (uint8_t)(sParseLen - i);
            }
            return;
        }
    }
    sParseLen = 0;
}

static void SbusUnpackChannels11(const uint8_t *payload22, uint16_t *out16)
{
    // Same 16ch x 11bit packing as CRSF RC_CHANNELS_PACKED payload.
    uint8_t bitsMerged = 0;
    uint32_t readValue = 0;
    unsigned readByteIndex = 0;
    const unsigned srcBits = 11;
    const uint32_t mask = (1u << srcBits) - 1u;

    for (unsigned ch = 0; ch < 16; ch++) {
        while (bitsMerged < srcBits) {
            uint8_t b = payload22[readByteIndex++];
            readValue |= ((uint32_t)b) << bitsMerged;
            bitsMerged += 8;
        }
        out16[ch] = (uint16_t)(readValue & mask);
        readValue >>= srcBits;
        bitsMerged -= srcBits;
    }
}

static inline uint16_t SbusToCrsfIfNeeded(uint16_t sbusVal, bool mapFromFullRange)
{
    if (!mapFromFullRange) {
        // still clamp to expected CRSF bounds to avoid downstream weirdness
        return (uint16_t)constrain(sbusVal, CRSF_CHANNEL_VALUE_MIN, CRSF_CHANNEL_VALUE_MAX);
    }
    // Map 0..2047 -> CRSF_CHANNEL_VALUE_MIN..CRSF_CHANNEL_VALUE_MAX
    uint16_t v = fmap(sbusVal, 0, 2047, CRSF_CHANNEL_VALUE_MIN, CRSF_CHANNEL_VALUE_MAX);
    return (uint16_t)constrain(v, CRSF_CHANNEL_VALUE_MIN, CRSF_CHANNEL_VALUE_MAX);
}

static void SBUSHandset_Begin(void)
{
    s_rx_wr = s_rx_rd = 0;
    sParseLen = 0;
    sLastGoodFrameUs = 0;

    Tk86xxSerialRegisterRxCallback(Sbus_UartRxCallback);
    handset.controllerConnected = false;
}

static void SBUSHandset_sendTelemetryToTX(uint8_t *data)
{
    (void)data;
    // SBUS has no upstream telemetry channel. Intentionally ignored.
}

static void SBUSHandset_registerParameterUpdateCallback(void (*callback)(uint8_t type, uint8_t index, uint8_t arg))
{
    (void)callback;
    // Not supported over SBUS.
}

static void SBUSHandset_registerCallbacks(void (*connectedCallback)(), void (*disconnectedCallback)(), void (*RecvModelUpdateCallback)(), void (*bindingCommandCallback)())
{
    handset.connected = connectedCallback;
    handset.disconnected = disconnectedCallback;
    handset.RecvModelUpdate = RecvModelUpdateCallback;
    handset.OnBindingCommand = bindingCommandCallback;
}

static bool SBUSHandset_IsArmed(void)
{
    return CRSF_to_BIT(ChannelData[4]);
}

static bool SBUSHandset_handleInput(void)
{
    const uint32_t nowUs = micros();

    // detect disconnect (SBUS should deliver frames continuously)
    if (handset.controllerConnected && sLastGoodFrameUs != 0 && (uint32_t)(nowUs - sLastGoodFrameUs) > 250000u) {
        handset.controllerConnected = false;
        if (handset.disconnected) handset.disconnected();
    }

    // pull bytes from ring buffer into parse buffer
    if (sParseLen < (uint8_t)sizeof(sParseBuf)) {
        uint16_t canRead = (uint16_t)(sizeof(sParseBuf) - sParseLen);
        uint16_t got = SbusReadBytes(&sParseBuf[sParseLen], canRead);
        sParseLen = (uint8_t)(sParseLen + got);
    } else {
        // parse buffer full, drop until next header
        SbusAlignToHeader(1);
    }

    SbusAlignToHeader(0);
    if (sParseLen < 3) return false;

    bool packetReceived = false;

    while (sParseLen >= SBUS_FRAME_LEN) {
        if (sParseBuf[0] != SBUS_HEADER) {
            SbusAlignToHeader(1);
            continue;
        }

        // footer is commonly 0x00, some variants use 0x04 (SBUS2). Be tolerant.
        const uint8_t footer = sParseBuf[SBUS_FRAME_LEN - 1u];
        if (!(footer == 0x00u || footer == 0x04u)) {
            SbusAlignToHeader(1);
            continue;
        }

        const uint8_t flags = sParseBuf[23];
        const bool lost = (flags & SBUS_FLAG_SIGNAL_LOSS) != 0;
        const bool failsafe = (flags & SBUS_FLAG_FAILSAFE_ACTIVE) != 0;
        (void)lost;
        (void)failsafe;

        uint16_t chRaw[16];
        SbusUnpackChannels11(&sParseBuf[1], chRaw);

        // Heuristic: if any primary channel looks like full-range 0..2047, map into CRSF bounds.
        bool mapFromFullRange = false;
        for (unsigned i = 0; i < 4; i++) {
            if (chRaw[i] < 100u || chRaw[i] > 1900u) {
                mapFromFullRange = true;
                break;
            }
        }

        for (unsigned i = 0; i < 16; i++) {
            ChannelData[i] = SbusToCrsfIfNeeded(chRaw[i], mapFromFullRange);
        }

        handset.RCdataLastRecv = nowUs;
        sLastGoodFrameUs = nowUs;

        if (!handset.controllerConnected) {
            handset.controllerConnected = true;
            if (handset.connected) handset.connected();
        }

        packetReceived = true;

        // consume this frame
        sParseLen = (uint8_t)(sParseLen - SBUS_FRAME_LEN);
        if (sParseLen > 0) {
            memmove(sParseBuf, &sParseBuf[SBUS_FRAME_LEN], sParseLen);
        }

        // align for next
        SbusAlignToHeader(0);
    }

    if (packetReceived && handset.RCdataCallback) {
        handset.RCdataCallback();
    }

    return packetReceived;
}

void Handset_Init(Handset_t *h)
{
    (void)h;

    handset.Begin = SBUSHandset_Begin;
    handset.handleInput = SBUSHandset_handleInput;
    handset.sendTelemetryToTX = SBUSHandset_sendTelemetryToTX;
    handset.registerParameterUpdateCallback = SBUSHandset_registerParameterUpdateCallback;
    handset.IsArmed = SBUSHandset_IsArmed;
    handset.registerCallbacks = SBUSHandset_registerCallbacks;
}

#endif // USE_SBUS_PROTOCOL || USE_INVERTED_SBUS_PROTOCOL

