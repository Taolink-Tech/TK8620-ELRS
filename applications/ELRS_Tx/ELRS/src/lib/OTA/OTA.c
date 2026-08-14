/**
 * This file is part of ExpressLRS
 * See https://github.com/AlessandroAU/ExpressLRS
 *
 * This file provides utilities for packing and unpacking the data to
 * be sent over the radio link.
 */

#include "OTA.h"
#include "common.h"
#include "CRSF.h"
#include "crc.h"
#include "logging.h"
#include "crsf_protocol.h"
#include "helpers.h"
#include "unified_config.h"
#include <string.h>

_Static_assert(sizeof(OTA_Packet4_s) == OTA4_PACKET_SIZE, "OTA4 packet struct is invalid");
_Static_assert(sizeof(OTA_Packet8_s) == OTA8_PACKET_SIZE, "OTA8 packet struct is invalid");
_Static_assert(ELRS8_TELEMETRY_BYTES_PER_CALL == AIRPORT_OTA_MAX_PAYLOAD,
               "AirPort OTA payload size changed");

bool OtaIsFullRes;
volatile uint8_t OtaNonce;
uint16_t OtaCrcInitializer;
OtaSwitchMode_e OtaSwitchModeCurrent;

// // CRC
// static Crc2Byte ota_crc;
ValidatePacketCrc_t OtaValidatePacketCrc;
GeneratePacketCrc_t OtaGeneratePacketCrc;

void OtaUpdateCrcInitFromUid()
{
    OtaCrcInitializer = (UID[4] << 8) | UID[5];
    OtaCrcInitializer ^= OTA_VERSION_ID;
#if ELRS_HAS_AIRPORT
    if (UnifiedConfig_IsAirport()) {
        OtaCrcInitializer ^= AIRPORT_CRC_DOMAIN;
    }
#endif
}

static inline uint8_t HybridWideNonceToSwitchIndex(uint8_t const nonce)
{
    // Returns the sequence (0 to 7, then 0 to 7 rotated left by 1):
    // 0, 1, 2, 3, 4, 5, 6, 7,
    // 1, 2, 3, 4, 5, 6, 7, 0
    // Because telemetry can occur on every 2, 4, 8, 16, 32, 64, 128th packet
    // this makes sure each of the 8 values is sent at least once every 16 packets
    // regardless of the TLM ratio
    // Index 7 also can never fall on a telemetry slot
    return ((nonce & 0b111) + ((nonce >> 3) & 0b1)) % 8;
}

// // Current ChannelData generator function being used by TX
PackChannelData_t OtaPackChannelData;
// #if defined(DEBUG_RCVR_LINKSTATS)
// static uint32_t packetCnt;
// #endif

// /******** Decimate 11bit to 10bit functions ********/
typedef uint32_t (*Decimate11to10_fn)(uint32_t ch11bit);

static uint32_t Decimate11to10_Limit(uint32_t ch11bit)
{
    // Limit 10-bit result to the range CRSF_CHANNEL_VALUE_MIN/MAX
    return CRSF_to_UINT10(constrain(ch11bit, CRSF_CHANNEL_VALUE_MIN, CRSF_CHANNEL_VALUE_MAX));
}

static uint32_t Decimate11to10_Div2(uint32_t ch11bit)
{
    // Simple divide-by-2 to discard the bit
    return ch11bit >> 1;
}

/***
 * @brief: Pack 4x 11-bit channel array into 4x 10 bit channel struct
 * @desc: Values are packed little-endianish such that bits A987654321 -> 87654321, 000000A9
 *        which is compatible with the 10-bit CRSF subset RC frame structure (0x17) in
 *        Betaflight, but depends on which decimate function is used if it is legacy or CRSFv3 10-bit
 *        destChannels4x10 must be zeroed before this call, the channels are ORed into it
 ***/
static void PackUInt11ToChannels4x10(uint32_t const * const src, OTA_Channels_4x10 * const destChannels4x10, Decimate11to10_fn decimate)
{
    const unsigned DEST_PRECISION = 10; // number of bits for each dest, must be <SRC
    uint8_t *dest = (uint8_t *)destChannels4x10;
    *dest = 0;
    unsigned destShift = 0;
    for (unsigned ch=0; ch<4; ++ch)
    {
        // Convert to DEST_PRECISION value
        unsigned chVal = decimate(src[ch]);

        // Put the low bits in any remaining dest capacity
        *dest++ |= chVal << destShift;

        // Shift the high bits down and place them into the next dest byte
        unsigned srcBitsLeft = DEST_PRECISION - 8 + destShift;
        *dest = chVal >> (DEST_PRECISION - srcBitsLeft);
        // Next dest should be shifted up by the bits consumed
        // if destShift == 8 then everything should reset for another set
        // but this code only expects to do the 4 channels -> 5 bytes evenly
        destShift = srcBitsLeft;
    }
}

static void PackChannelDataHybridCommon(OTA_Packet4_s * const ota4, const uint32_t *channelData)
{
    ota4->type = PACKET_TYPE_RCDATA;
#if defined(DEBUG_RCVR_LINKSTATS)
    // Incremental packet counter for verification on the RX side, 32 bits shoved into CH1-CH4
    ota4->dbg_linkstats.packetNum = packetCnt++;
#else
    // CRSF input is 11bit and OTA will carry only 10bit. Discard the Extended Limits (E.Limits)
    // range and use the full 10bits to carry only 998us - 2012us
    PackUInt11ToChannels4x10(&channelData[0], &ota4->rc.ch, &Decimate11to10_Limit);
    ota4->rc.ch4 = CRSF_to_BIT(channelData[4]);
#endif /* !DEBUG_RCVR_LINKSTATS */
}

/**
 * Hybrid switches packet encoding for sending over the air
 *
 * Analog channels are reduced to 10 bits to allow for switch encoding
 * Switch[0] is sent on every packet.
 * A 3 bit switch index and 3-4 bit value is used to send the remaining switches
 * in a round-robin fashion.
 *
 * Inputs: channelData, TelemetryStatus
 * Outputs: OTA_Packet4_s, side-effects the sentSwitch value
 */
// The next switch index to send, where 0=AUX2 and 6=AUX8
static uint8_t Hybrid8NextSwitchIndex;
static void GenerateChannelDataHybrid8(OTA_Packet_s * const otaPktPtr, const uint32_t *channelData, bool const TelemetryStatus, uint8_t const tlmDenom)
{
    (void)tlmDenom;

    OTA_Packet4_s * const ota4 = &otaPktPtr->std;
    PackChannelDataHybridCommon(ota4, channelData);

    // Actually send switchIndex - 1 in the packet, to shift down 1-7 (0b111) to 0-6 (0b110)
    // If the two high bits are 0b11, the receiver knows it is the last switch and can use
    // that bit to store data
    uint8_t bitclearedSwitchIndex = Hybrid8NextSwitchIndex;
    uint8_t value;
    // AUX8 is High Resolution 16-pos (4-bit)
    if (bitclearedSwitchIndex == 6)
        value = CRSF_to_N(channelData[6 + 1 + 4], 16);
    else
        value = CRSF_to_SWITCH3b(channelData[bitclearedSwitchIndex + 1 + 4]);

    ota4->rc.switches =
        TelemetryStatus << 6 |
        // tell the receiver which switch index this is
        bitclearedSwitchIndex << 3 |
        // include the switch value
        value;

    // update the sent value
    Hybrid8NextSwitchIndex = (bitclearedSwitchIndex + 1) % 7;
}

/**
 * Return the OTA value respresentation of the switch contained in ChannelData
 * Switches 1-6 (AUX2-AUX7) are 6 or 7 bit depending on the lowRes parameter
 */
static uint8_t HybridWideSwitchToOta(const uint32_t *channelData, uint8_t const switchIdx, bool const lowRes)
{
    uint16_t ch = channelData[switchIdx + 4];
    uint8_t binCount = (lowRes) ? 64 : 128;
    ch = CRSF_to_N(ch, binCount);
    if (lowRes)
        return ch & 0b111111; // 6-bit
    else
        return ch & 0b1111111; // 7-bit
}

/**
 * HybridWide switches packet encoding for sending over the air
 *
 * Analog channels are reduced to 10 bits to allow for switch encoding
 * Switch[0] is sent on every packet.
 * A 6 or 7 bit switch value is used to send the remaining switches
 * in a round-robin fashion.
 *
 * Inputs: cchannelData, TelemetryStatus
 * Outputs: OTA_Packet4_s
 **/
static void GenerateChannelDataHybridWide(OTA_Packet_s * const otaPktPtr, const uint32_t *channelData, bool const TelemetryStatus, uint8_t const tlmDenom)
{
    OTA_Packet4_s * const ota4 = &otaPktPtr->std;
    PackChannelDataHybridCommon(ota4, channelData);

    uint8_t telemBit = TelemetryStatus << 6;
    uint8_t nextSwitchIndex = HybridWideNonceToSwitchIndex(OtaNonce);
    uint8_t value;
    // Using index 7 means the telemetry bit will always be sent in the packet
    // preceding the RX's telemetry slot for all tlmDenom >= 8
    // For more frequent telemetry rates, include the bit in every
    // packet and degrade the value to 6-bit
    // (technically we could squeeze 7-bits in for 2 channels with tlmDenom=4)
    if (nextSwitchIndex == 7)
    {
        value = telemBit | CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_TX_Power;
    }
    else
    {
        bool telemInEveryPacket = (tlmDenom > 1) && (tlmDenom < 8);
        value = HybridWideSwitchToOta(channelData, nextSwitchIndex + 1, telemInEveryPacket);
        if (telemInEveryPacket)
            value |= telemBit;
    }

    ota4->rc.switches = value;
}

static void GenerateChannelData8ch12ch(OTA_Packet8_s * const ota8, const uint32_t *channelData, bool const TelemetryStatus, bool const isHighAux)
{
    // All channel data is 10 bit apart from AUX1 which is 1 bit
    ota8->rc.packetType = PACKET_TYPE_RCDATA;
    ota8->rc.telemetryStatus = TelemetryStatus;
    // uplinkPower has 8 items but only 3 bits, but 0 is 0 power which we never use, shift 1-8 -> 0-7
    ota8->rc.uplinkPower = (uint8_t)(constrain(CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_TX_Power, 1, 8) - 1);
    ota8->rc.isHighAux = isHighAux;
    ota8->rc.ch4 = CRSF_to_BIT(channelData[4]);
#if defined(DEBUG_RCVR_LINKSTATS)
    // Incremental packet counter for verification on the RX side, 32 bits shoved into CH1-CH4
    ota8->dbg_linkstats.packetNum = packetCnt++;
#else
    // Sources:
    // 8ch always: low=0 high=5
    // 12ch isHighAux=false: low=0 high=5
    // 12ch isHighAux=true:  low=0 high=9
    // 16ch isHighAux=false: low=0 high=4
    // 16ch isHighAux=true:  low=8 high=12
    uint8_t chSrcLow;
    uint8_t chSrcHigh;
    if (OtaSwitchModeCurrent == smHybridOr16ch) {
        // 16ch mode
        if (isHighAux) {
            chSrcLow = 8;
            chSrcHigh = 12;
        } else {
            chSrcLow = 0;
            chSrcHigh = 4;
        }
    } else {
        chSrcLow = 0;
        chSrcHigh = isHighAux ? 9 : 5;
    }
    PackUInt11ToChannels4x10(&channelData[chSrcLow], &ota8->rc.chLow, &Decimate11to10_Div2);
    PackUInt11ToChannels4x10(&channelData[chSrcHigh], &ota8->rc.chHigh, &Decimate11to10_Div2);
#endif
}

static void GenerateChannelData8ch(OTA_Packet_s * const otaPktPtr, const uint32_t *channelData, bool const TelemetryStatus, uint8_t const tlmDenom)
{
    (void)tlmDenom;

    GenerateChannelData8ch12ch((OTA_Packet8_s * const)otaPktPtr, channelData, TelemetryStatus, false);
}

static bool FullResIsHighAux;
// #if defined(UNIT_TEST)
// void OtaSetFullResNextChannelSet(bool next) { FullResIsHighAux = next; }
// #endif
static void GenerateChannelData12ch(OTA_Packet_s * const otaPktPtr, const uint32_t *channelData, bool const TelemetryStatus, uint8_t const tlmDenom)
{
    (void)tlmDenom;

    // Every time this function is called, the opposite high Aux channels are sent
    // This tries to ensure a fair split of high and low aux channels packets even
    // at 1:2 ratio and around sync packets
    GenerateChannelData8ch12ch((OTA_Packet8_s * const)otaPktPtr, channelData, TelemetryStatus, FullResIsHighAux);
    FullResIsHighAux = !FullResIsHighAux;
}
// #endif

bool ValidatePacketCrcFull(OTA_Packet_s * const otaPktPtr)
{
    // DBGLN("ValidatePacketCrcFull, otaPktPtr->full.crc = %x, OtaCrcInitializer = %u", otaPktPtr->full.crc, OtaCrcInitializer);
    uint16_t const calculatedCRC = Crc2ByteCalc((uint8_t*)otaPktPtr, OTA8_CRC_CALC_LEN, OtaCrcInitializer);
    // DBGLN("ValidatePacketCrcFull, calculatedCRC = %x", calculatedCRC);
    return otaPktPtr->full.crc == calculatedCRC;
}

static bool ValidatePacketCrcStd(OTA_Packet_s * const otaPktPtr)
{
    uint8_t backupCrcHigh = otaPktPtr->std.crcHigh;

    uint16_t const inCRC = ((uint16_t)otaPktPtr->std.crcHigh << 8) + otaPktPtr->std.crcLow;
    // In the public TK8620 build this byte is always cleared before the CRC is
    // calculated.
    {
        otaPktPtr->std.crcHigh = 0;
    }
    uint16_t const calculatedCRC = Crc2ByteCalc((uint8_t*)otaPktPtr, OTA4_CRC_CALC_LEN, OtaCrcInitializer);

    otaPktPtr->std.crcHigh = backupCrcHigh;
    
    return inCRC == calculatedCRC;
}

void GeneratePacketCrcFull(OTA_Packet_s * const otaPktPtr)
{
    otaPktPtr->full.crc = Crc2ByteCalc((uint8_t*)otaPktPtr, OTA8_CRC_CALC_LEN, OtaCrcInitializer);
    // DBGLN("GeneratePacketCrcFull, otaPktPtr->full.crc = %x, OtaCrcInitializer = %u", otaPktPtr->full.crc, OtaCrcInitializer);
}

static void GeneratePacketCrcStd(OTA_Packet_s * const otaPktPtr)
{
    uint16_t crc = Crc2ByteCalc((uint8_t*)otaPktPtr, OTA4_CRC_CALC_LEN, OtaCrcInitializer);
    otaPktPtr->std.crcHigh = (crc >> 8);
    otaPktPtr->std.crcLow  = crc;
}

void OtaUpdateSerializers(OtaSwitchMode_e const switchMode, uint8_t packetSize)
{
    OtaIsFullRes = (packetSize == OTA8_PACKET_SIZE);

    if (OtaIsFullRes) { // is8ch
        OtaValidatePacketCrc = &ValidatePacketCrcFull;
        OtaGeneratePacketCrc = &GeneratePacketCrcFull;
        Crc2ByteInit(16, ELRS_CRC16_POLY);

        if (switchMode == smWideOr8ch)
            OtaPackChannelData = &GenerateChannelData8ch;
        else
            OtaPackChannelData = &GenerateChannelData12ch;
    } else { 
        OtaValidatePacketCrc = &ValidatePacketCrcStd;
        OtaGeneratePacketCrc = &GeneratePacketCrcStd;
        Crc2ByteInit(14, ELRS_CRC14_POLY);

        if (switchMode == smWideOr8ch)
        {
            OtaPackChannelData = &GenerateChannelDataHybridWide;
        } // !is8ch and smWideOr8ch

        else
        {
            OtaPackChannelData = &GenerateChannelDataHybrid8;
        } // !is8ch and smHybridOr16ch
    }

    OtaSwitchModeCurrent = switchMode;
}

#if ELRS_HAS_AIRPORT
uint8_t OtaPackAirportData(OTA_Packet_s *otaPktPtr, AirportFifo_t *inputBuffer)
{
    uint16_t available = AirportFifo_Size(inputBuffer);
    uint8_t count = AirportPayloadLengthClamp(available);

    memset(otaPktPtr, 0, sizeof(*otaPktPtr));
    otaPktPtr->full.airport.packetType = PACKET_TYPE_TLM;
    otaPktPtr->full.airport.count = count;
    AirportFifo_PopBytes(inputBuffer, otaPktPtr->full.airport.payload, count);
    return count;
}

bool OtaUnpackAirportData(const OTA_Packet_s *otaPktPtr, AirportFifo_t *outputBuffer)
{
    uint8_t count = otaPktPtr->full.airport.count;
    if (!AirportPayloadLengthIsValid(count)) {
        return false;
    }

    return AirportFifo_PushBytes(outputBuffer, otaPktPtr->full.airport.payload, count) == count;
}
#endif
