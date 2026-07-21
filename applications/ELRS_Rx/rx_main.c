#include <string.h>
#include <stdbool.h>
#include "tk86xx_api.h"
#include "flash_hal.h"
#include "tk86xx_platform.h"
#include "wdt_hal.h"
#include "eclic_hal.h"
#include "crc.h"
#include "FHSS.h"
#include "OTA.h"
#include "common.h"
#include "device.h"
#include "helpers.h"
#include "SerialCRSF.h"
#include "devSerialIO.h"
#include "SerialIO.h"
#include "devRadioRx.h"
#include "elrs_eeprom.h"
#include "config.h"
#include "devLED.h"
#include "devButton.h"
#include "options.h"
#include "msptypes.h"
#include "deferred.h"
#include "telemetry.h"
#include "tk86xx_api.h"
#include "stubborn_sender.h"
#include "stubborn_receiver.h"
#include "CRSF.h"
#include "POWERMGNT.h"
#include "dynpower.h"
#include "src/lib/LUA/devLUA.h"
#include "src/lib/LUA/lua.h"
#include "LowPassFilter.h"
#include "MeanAccumulator.h"
#include "devAnalogVbat.h"

device_affinity_t ui_devices[] = {
    {&Serial0_device, 1},
    {&ratioRxDevice, 0},
    {&LED_device, 0},
    {&Button_device, 0},
#if defined(USE_ANALOG_VBAT)
    {&AnalogVbat_device, 0},
#endif
    {&LUA_device, 0},
};

SerialIO_t serialIO;
ELRS_EEPROM_t eeprom;
RxConfig_t rxConfig;
bool hardwareConfigured = true;
bool BindingModeRequest = false;
Telemetry_t telemetry;
StubbornSender_t TelemetrySender;
static uint8_t telemetryBurstCount;
static uint8_t telemetryBurstMax;
uint8_t currentTelemetryPayload[CRSF_MAX_PACKET_LEN];
static uint8_t scanIndex;
static uint8_t NextTelemetryType = ELRS_TELEMETRY_TYPE_LINK;
static uint8_t antenna = 0;    // which antenna is currently in use
StubbornReceiver_t MspReceiver;
static uint8_t     MspData[ELRS_MSP_BUFFER];
MeanAccumulator_t SnrMean;
static bool telemBurstValid;
static uint8_t ExpressLRS_nextAirRateIndex;
static int8_t SwitchModePending = 0;
LPF_t LPF_UplinkRSSI0 = {
    .Beta = 5,
    .FP_Shift = 5,
    .NeedReset = true,
};

static void SetRFLinkRate(uint8_t index, bool bindMode);

#if SENSI_TEST
#define SENSI_LQ_WINDOW_SIZE 100U

static void PrintSensiStats(SignalQuality_t *signalQuality, bool hasPayload)
{
    static uint8_t rxWindow[SENSI_LQ_WINDOW_SIZE] = {0};
    static uint32_t rxWindowIndex = 0;
    static uint32_t rxWindowCount = 0;
    static uint32_t validRxSlots = 0;
    const uint8_t currentValid = (hasPayload && signalQuality) ? 1U : 0U;

    if (rxWindowCount < SENSI_LQ_WINDOW_SIZE) {
        rxWindowCount++;
    } else if (rxWindow[rxWindowIndex]) {
        validRxSlots--;
    }

    rxWindow[rxWindowIndex] = currentValid;
    validRxSlots += currentValid;
    rxWindowIndex = (rxWindowIndex + 1U) % SENSI_LQ_WINDOW_SIZE;

    uint32_t percent = (validRxSlots * 100U) / rxWindowCount;
    int snr = (hasPayload && signalQuality) ? signalQuality->snr : 0;
    int rssi = (hasPayload && signalQuality) ? signalQuality->rssi : 0;
    tk_printf("%d,%d,%d%%\r\n", snr, rssi, (int)percent);
}
#endif

#define RX_LUA_VERSION_FIELD_ID               2U
#define RX_VERSION_PUSH_FAST_WINDOW_MS    10000U
#define RX_VERSION_PUSH_FAST_INTERVAL_MS   1000U
#define RX_VERSION_PUSH_SLOW_INTERVAL_MS  10000U

static bool rxVersionPushConnected;
static uint32_t rxVersionPushConnectedMs;
static uint32_t rxVersionPushLastMs;

#define UPLINK_LQ_HISTORY_SIZE        100U
#define UPLINK_LQ_HISTORY_MIN_SIZE    10U
#define UPLINK_LQ_TARGET_WINDOW_MS  1500U

typedef struct {
    uint8_t windowSize;
    uint8_t lq;
    uint8_t index;
    uint8_t count;
    uint32_t mask;
    uint32_t history[(UPLINK_LQ_HISTORY_SIZE + 31U) / 32U];
} UplinkLqTracker_t;

static UplinkLqTracker_t s_uplinkLqTracker;

static uint8_t UplinkLqTracker_ComputeWindowSize(expresslrs_mod_settings_t const *modParams)
{
    if ((modParams == 0) || (modParams->interval <= 0)) {
        return UPLINK_LQ_HISTORY_SIZE;
    }

    uint32_t rxSlotIntervalUs = (uint32_t)modParams->interval;
    const uint32_t tlmDenom = TLMratioEnumToValue((expresslrs_tlm_ratio_e)modParams->TLMinterval);

    if (tlmDenom > 1U) {
        rxSlotIntervalUs = (((uint32_t)modParams->interval * tlmDenom) + (tlmDenom - 2U)) / (tlmDenom - 1U);
    }

    uint32_t windowSize = ((UPLINK_LQ_TARGET_WINDOW_MS * 1000U) + rxSlotIntervalUs - 1U) / rxSlotIntervalUs;
    if (windowSize < UPLINK_LQ_HISTORY_MIN_SIZE) {
        windowSize = UPLINK_LQ_HISTORY_MIN_SIZE;
    } else if (windowSize > UPLINK_LQ_HISTORY_SIZE) {
        windowSize = UPLINK_LQ_HISTORY_SIZE;
    }

    return (uint8_t)windowSize;
}

static void UplinkLqTracker_Reset(uint8_t windowSize)
{
    memset(&s_uplinkLqTracker, 0, sizeof(s_uplinkLqTracker));
    if ((windowSize == 0U) || (windowSize > UPLINK_LQ_HISTORY_SIZE)) {
        windowSize = UPLINK_LQ_HISTORY_SIZE;
    }
    s_uplinkLqTracker.windowSize = windowSize;
    s_uplinkLqTracker.mask = 1U;
}

static uint8_t UplinkLqTracker_GetLq(void)
{
    if (s_uplinkLqTracker.count == 0U) {
        return 0U;
    }

    return (uint8_t)(((uint32_t)s_uplinkLqTracker.lq * 100U) / s_uplinkLqTracker.count);
}

static void UplinkLqTracker_Advance(void)
{
    s_uplinkLqTracker.mask <<= 1U;
    if (s_uplinkLqTracker.mask == 0U) {
        s_uplinkLqTracker.mask = 1U;
        s_uplinkLqTracker.index++;
    }

    const uint8_t wrapIndex = s_uplinkLqTracker.windowSize / 32U;
    const uint8_t wrapBit = s_uplinkLqTracker.windowSize % 32U;
    if ((wrapBit == 0U && s_uplinkLqTracker.index == wrapIndex) ||
        ((wrapBit != 0U) && (s_uplinkLqTracker.index == wrapIndex) &&
         ((s_uplinkLqTracker.mask & (1UL << wrapBit)) != 0U))) {
        s_uplinkLqTracker.index = 0U;
        s_uplinkLqTracker.mask = 1U;
    }

    if ((s_uplinkLqTracker.history[s_uplinkLqTracker.index] & s_uplinkLqTracker.mask) != 0U) {
        s_uplinkLqTracker.history[s_uplinkLqTracker.index] &= ~s_uplinkLqTracker.mask;
        s_uplinkLqTracker.lq--;
    }

    if (s_uplinkLqTracker.count < s_uplinkLqTracker.windowSize) {
        s_uplinkLqTracker.count++;
    }
}

static void UplinkLqTracker_Record(bool receivedValidPacket)
{
    if (receivedValidPacket &&
        ((s_uplinkLqTracker.history[s_uplinkLqTracker.index] & s_uplinkLqTracker.mask) == 0U)) {
        s_uplinkLqTracker.history[s_uplinkLqTracker.index] |= s_uplinkLqTracker.mask;
        s_uplinkLqTracker.lq++;
    }

    UplinkLqTracker_Advance();
}

static void ProcessRfPacket_RC(OTA_Packet_s const * const otaPktPtr)
{
    // Must be fully connected to process RC packets, prevents processing RC
    // during sync, where packets can be received before connection
    if (connectionState != connected || SwitchModePending)
        return;

    bool telemetryConfirmValue = OtaUnpackChannelData(otaPktPtr, ChannelData, ExpressLRS_currTlmDenom);
    TelemetrySender.ConfirmCurrentPayload(telemetryConfirmValue);

    // No channels packets to the FC or PWM pins if no model match
    if (connectionHasModelMatch)
    {
        // if (ExpressLRS_currAirRate_Modparams->numOfSends == 1)
        {
            crsfRCFrameAvailable();
            // teamrace is only checked for servos because the teamrace model select logic only runs
            // when new frames are available, and will decide later if the frame will be forwarded
            // if (teamraceHasModelMatch)
            //     servoNewChannelsAvailable();
        }
        // else if (!LQCalcDVDA.currentIsSet())
        // {
        //     LQCalcDVDA.add();
        // }
        #if defined(DEBUG_RCVR_LINKSTATS)
        debugRcvrLinkstatsPending = true;
        #endif
    }
}

static void OnELRSBindMSP(uint8_t* newUid4)
{
    // Binding over MSP only contains 4 bytes due to packet size limitations, clear out any leading bytes
    UID[0] = 0;
    UID[1] = 0;
    for (unsigned i = 0; i < 4; i++)
    {
        UID[i + 2] = newUid4[i];
    }

    DBGLN("New UID = %02x, %02x, %02x, %02x, %02x, %02x", UID[0], UID[1], UID[2], UID[3], UID[4], UID[5]);

    // Set new UID in eeprom
    // EEPROM commit will happen on the main thread in ExitBindingMode()
    rxConfig.SetUID(UID);
}

static void ProcessRfPacket_MSP(OTA_Packet_s const * const otaPktPtr)
{
    uint8_t packageIndex;
    uint8_t const * payload;
    uint8_t dataLen;

    if (OtaIsFullRes) {
        packageIndex = otaPktPtr->full.msp_ul.packageIndex;
        payload = otaPktPtr->full.msp_ul.payload;
        dataLen = sizeof(otaPktPtr->full.msp_ul.payload);
        packageIndex &= ELRS8_TELEMETRY_MAX_PACKAGES;
    } else {
        packageIndex = otaPktPtr->std.msp_ul.packageIndex;
        payload = otaPktPtr->std.msp_ul.payload;
        dataLen = sizeof(otaPktPtr->std.msp_ul.payload);
        packageIndex &= ELRS4_TELEMETRY_MAX_PACKAGES;
    }

    // Always examine MSP packets for bind information if in bind mode
    // [1] is the package index, first packet of the MSP
    // DBGLN("packageIndex = %u, payload[0] = %u", packageIndex, payload[0]);
    if (InBindingMode && packageIndex == 1 && payload[0] == MSP_ELRS_BIND)
    {
        OnELRSBindMSP((uint8_t *)&payload[1]);
        return;
    }

    // Must be fully connected to process MSP, prevents processing MSP
    // during sync, where packets can be received before connection
    // if (connectionState != connected)
    //     return;

    bool currentMspConfirmValue = MspReceiver.GetCurrentConfirm();
    MspReceiver.ReceiveData(packageIndex, payload, dataLen);
    if (currentMspConfirmValue != MspReceiver.GetCurrentConfirm()) {
        NextTelemetryType = ELRS_TELEMETRY_TYPE_LINK;
    }
}

static void getRFlinkInfo(SignalQuality_t *signalQuality)
{
    int32_t rssiDBM = signalQuality->rssi;
    // DBGLN("rssiDBM = %d", rssiDBM);

    if (antenna == 0)
    {
        #if !defined(DEBUG_RCVR_LINKSTATS)
        rssiDBM = LPF_update(&LPF_UplinkRSSI0, rssiDBM);
        // DBGLN("LPF rssiDBM = %d", rssiDBM);
        #endif
        if (rssiDBM > 0) rssiDBM = 0;
        // BetaFlight/iNav expect positive values for -dBm (e.g. -80dBm -> sent as 80)
        CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_RSSI_1 = -rssiDBM;
    }

    MeanAccumulator_add(&SnrMean, signalQuality->snr);

    CRSF_GetLinkStatistics()->crsfLinkStatistics.active_antenna = antenna;
    CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_SNR = signalQuality->snr; // possibly overriden below
    CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_Link_quality = UplinkLqTracker_GetLq();
    CRSF_GetLinkStatistics()->crsfLinkStatistics.rf_Mode = ExpressLRS_currAirRate_Modparams->enum_rate;
    //DBGLN(CRSF::LinkStatistics.uplink_RSSI_1);
    #if defined(DEBUG_BF_LINK_STATS)
    CRSF_GetLinkStatistics()->crsfLinkStatistics.downlink_RSSI_1 = debug1;
    CRSF_GetLinkStatistics()->crsfLinkStatistics.downlink_Link_quality = debug2;
    CRSF_GetLinkStatistics()->crsfLinkStatistics.downlink_SNR = debug3;
    CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_RSSI_2 = debug4;
    #endif

    #if defined(DEBUG_RCVR_LINKSTATS)
    // DEBUG_RCVR_LINKSTATS gets full precision SNR, override the value
    CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_SNR = signalQuality->snr;
    debugRcvrLinkstatsFhssIdx = FHSSsequence[FHSSptr];
    #endif
}

static void TentativeConnection(unsigned long now)
{
    // PFDloop.reset();
    // connectionState = tentative;
    // connectionHasModelMatch = false;
    // RXtimerState = tim_disconnected;
    // DBGLN("tentative conn");
    // PfdPrevRawOffset = 0;
    // LPF_Offset.init(0);
    // SnrMean.reset();
    // RFmodeLastCycled = now; // give another 3 sec for lock to occur

    // Use this rate as the initial rate next time if we connected on it
    rxConfig.SetRateInitialIdx(ExpressLRS_nextAirRateIndex);
    // And stop counting toward binding mode
    // if (config.GetPowerOnCounter() != 0)
    // {
    //     config.SetPowerOnCounter(0);
    // }

    // The caller MUST call hwTimer::resume(). It is not done here because
    // the timer ISR will fire immediately and preempt any other code
}

static void updateSwitchModePendingFromOta(uint8_t newSwitchMode)
{
    if (OtaSwitchModeCurrent == newSwitchMode)
    {
        // Cancel any switch if pending
        SwitchModePending = 0;
        // DBGLN("sm update return, newSwitchMode=%d", newSwitchMode);
        return;
    }

    // One is added to the mode because SwitchModePending==0 means no switch pending
    // and that's also a valid switch mode. The 1 is removed when this is handled.
    // A negative SwitchModePending means not to switch yet
    int8_t newSwitchModePending = -(int8_t)newSwitchMode - 1;
    DBGLN("newSwitchMode = %d, newSwitchModePending = %d", newSwitchMode, newSwitchModePending);

    // Switch mode can be changed while disconnected
    // OR there are two sync packets with the same new switch mode,
    // as a "confirm". No RC packets are processed until
    if (connectionState == disconnected ||
        SwitchModePending == newSwitchModePending)
    {
        // Add one to the mode because SwitchModePending==0 means no switch pending
        // and that's also a valid switch mode. The 1 is removed when this is handled
        SwitchModePending = newSwitchMode + 1;
    }
    else
    {
        // Save the negative version of the new switch mode to compare
        // against on the next SYNC packet, but do not switch yet
        SwitchModePending = newSwitchModePending;
    }
    DBGLN("SwitchModePending = %d", SwitchModePending);
}

static bool ProcessRfPacket_SYNC(uint32_t const now, OTA_Sync_s const * const otaSync)
{
    // Verify the first two of three bytes of the binding ID, which should always match
    if (otaSync->UID3 != UID[3] || otaSync->UID4 != UID[4])
    {
        DBGLN("ProcessRfPacket_SYNC: UID3 or UID4 mismatch");
        return false;
    }

    // The third byte will be XORed with inverse of the ModelId if ModelMatch is on
    // Only require the first 18 bits of the UID to match to establish a connection
    // but the last 6 bits must modelmatch before sending any data to the FC
    if ((otaSync->UID5 & ~MODELMATCH_MASK) != (UID[5] & ~MODELMATCH_MASK)) 
    {
        DBGLN("ProcessRfPacket_SYNC: UID5 mismatch");
        return false;
    }

    // LastSyncPacket = now;

    // DBGLN("SYNC rateIndex: %d", otaSync->rateIndex);
    // Will change the packet air rate in loop() if this changes
    ExpressLRS_nextAirRateIndex = otaSync->rateIndex;
    updateSwitchModePendingFromOta(otaSync->switchEncMode);

    // Update TLM ratio, should never be TLM_RATIO_STD/DISARMED, the TX calculates the correct value for the RX
    expresslrs_tlm_ratio_e TLMrateIn = (expresslrs_tlm_ratio_e)(otaSync->newTlmRatio + (uint8_t)TLM_RATIO_NO_TLM);
    uint8_t TlmDenom = TLMratioEnumToValue(TLMrateIn);
    if (ExpressLRS_currTlmDenom != TlmDenom)
    {
        // DBGLN("New TLMrate 1:%u", TlmDenom);
        ExpressLRS_currTlmDenom = TlmDenom;
        telemBurstValid = false;
        tlmChanged = true;
        devicesTriggerEvent();
    }

    // modelId = 0xff indicates modelMatch is disabled, the XOR does nothing in that case
    uint8_t modelXor = (~rxConfig.GetModelId()) & MODELMATCH_MASK;
    bool modelMatched = otaSync->UID5 == (UID[5] ^ modelXor);
    // DBGLN("MM %u=%u %d", otaSync->UID5, UID[5], modelMatched);

    if (connectionState == disconnected
        || OtaNonce != otaSync->nonce
        || FHSSgetCurrIndex() != otaSync->fhssIndex
        || connectionHasModelMatch != modelMatched)
    {
    //     //DBGLN("\r\n%ux%ux%u", OtaNonce, otaPktPtr->sync.nonce, otaPktPtr->sync.fhssIndex);
    //     FHSSsetCurrIndex(otaSync->fhssIndex);
    //     OtaNonce = otaSync->nonce;
        TentativeConnection(now);
        // connectionHasModelMatch must come after TentativeConnection, which resets it
        connectionHasModelMatch = modelMatched;
        return true;
    }

    return false;
}

static bool IsUplinkPacketCandidate(OTA_Packet_s const * const otaPktPtr)
{
    switch (otaPktPtr->std.type) {
    case PACKET_TYPE_RCDATA:
    case PACKET_TYPE_SYNC:
        return true;

    case PACKET_TYPE_MSPDATA:
        return InBindingMode || (connectionState != disconnected);

    default:
        return false;
    }
}

static bool ProcessRFPacket(SignalQuality_t *signalQuality)
{
    // uint32_t const beginProcessing = micros();

    OTA_Packet_s * const otaPktPtr = (OTA_Packet_s * const)s_data_buf;
    // DBGLN("ProcessRFPacket: packet type: %d", otaPktPtr->std.type);
    if (!IsUplinkPacketCandidate(otaPktPtr)) {
        return false;
    }

    if (!OtaValidatePacketCrc(otaPktPtr)) {
        if (otaPktPtr->std.type == PACKET_TYPE_RCDATA) {
            DBGLN("ProcessRFPacket: CRC error\r\n");
        }
        return false;
    }

    // PFDloop.extEvent(beginProcessing + PACKET_TO_TOCK_SLACK);

    // doStartTimer = false;
    unsigned long now = millis();

    // LastValidPacket = now;

    // DBGLN("Received packet type: %d", otaPktPtr->std.type);
    switch (otaPktPtr->std.type) {
    case PACKET_TYPE_RCDATA: //Standard RC Data Packet
        ProcessRfPacket_RC(otaPktPtr);
        if (connectionState != connected) {
            connectionState = connected;
            devicesTriggerEvent();
        }
        break;
    case PACKET_TYPE_MSPDATA:
        ProcessRfPacket_MSP(otaPktPtr);
        break;
    case PACKET_TYPE_SYNC: //sync packet from master
        // DBGLN("rcvd sync packet");
        // doStartTimer = 
        ProcessRfPacket_SYNC(now, OtaIsFullRes ? &otaPktPtr->full.sync.sync : &otaPktPtr->std.sync);
            // && !InBindingMode;
        break;
    default:
        break;
    }

    UplinkLqTracker_Record(true);

    // Store the LQ/RSSI/Antenna
    // Radio.GetLastPacketStats();
    getRFlinkInfo(signalQuality);

    // if (Radio.FrequencyErrorAvailable())
    // {
    // #if defined(RADIO_SX127X)
    //     int32_t tempFreqCorrection = HandleFreqCorr(Radio.GetFrequencyErrorbool());      // Adjusts FreqCorrection for RX freq offset
    //     // Teamp900 also needs to adjust its demood PPM
    //     Radio.SetPPMoffsetReg(tempFreqCorrection);
    // #endif /* RADIO_SX127X */
    // }

    // Received a packet, that's the definition of LQ
    // LQCalc.add();
    // Extend sync duration since we've received a packet at this rate
    // but do not extend it indefinitely
    // RFmodeCycleMultiplier = RFmodeCycleMultiplierSlow;

#if defined(DEBUG_RX_SCOREBOARD)
    if (otaPktPtr->std.type != PACKET_TYPE_SYNC) DBGW(connectionHasModelMatch ? 'R' : 'r');
#endif

    return true;
}

static void RXdoneISR(SignalQuality_t *signalQuality, bool isRxDataSlot, bool hasPayload)
{
    if (!isRxDataSlot) {
        return;
    }

#if SENSI_TEST
    PrintSensiStats(signalQuality, hasPayload);
    return;
#endif

    if (!hasPayload || (signalQuality == NULL) || !ProcessRFPacket(signalQuality)) {
        UplinkLqTracker_Record(false);
        CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_Link_quality = UplinkLqTracker_GetLq();
    }
}

static void LinkStatsToOta(OTA_LinkStats_s * const ls)
{
    // The value in linkstatistics is "positivized" (inverted polarity)
    // and must be inverted on the TX side. Positive values are used
    // so save a bit to encode which antenna is in use
    ls->uplink_RSSI_1 = CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_RSSI_1;    
    ls->uplink_RSSI_2 = CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_RSSI_2;
    ls->antenna = antenna;
    ls->modelMatch = connectionHasModelMatch;
    ls->lq = CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_Link_quality;
    ls->mspConfirm = MspReceiver.GetCurrentConfirm() ? 1 : 0;
#if defined(DEBUG_FREQ_CORRECTION)
    ls->SNR = FreqCorrection * 127 / FreqCorrectionMax;
#else
    if (MeanAccumulator_getCount(&SnrMean))
    {
        ls->SNR = MeanAccumulator_mean(&SnrMean);
    }
    else
    {
        ls->SNR = MeanAccumulator_previousMean(&SnrMean);
    }
#endif
}

static bool HandleSendTelemetryResponse(void)
{
    if ((connectionState == disconnected) || (ExpressLRS_currTlmDenom == 1) || !teamraceHasModelMatch)
    {
        return false; // don't bother sending tlm if disconnected or TLM is off
    }


    OTA_Packet_s otaPkt = {0};
    otaPkt.std.type = PACKET_TYPE_TLM;

    bool tlmQueued = false;
    tlmQueued = TelemetrySender.IsActive();

    if (NextTelemetryType == ELRS_TELEMETRY_TYPE_LINK || !tlmQueued)
    {
        OTA_LinkStats_s * ls;
        if (OtaIsFullRes)
        {
            otaPkt.full.tlm_dl.containsLinkStats = 1;
            ls = &otaPkt.full.tlm_dl.ul_link_stats.stats;
            // Include some advanced telemetry in the extra space
            // Note the use of `ul_link_stats.payload` vs just `payload`
            otaPkt.full.tlm_dl.packageIndex = TelemetrySender.GetCurrentPayload(otaPkt.full.tlm_dl.ul_link_stats.payload, sizeof(otaPkt.full.tlm_dl.ul_link_stats.payload));
        }
        else
        {
            otaPkt.std.tlm_dl.type = ELRS_TELEMETRY_TYPE_LINK;
            ls = &otaPkt.std.tlm_dl.ul_link_stats.stats;
        }
        LinkStatsToOta(ls);

        NextTelemetryType = ELRS_TELEMETRY_TYPE_DATA;
        // Start the count at 1 because the next will be DATA and doing +1 before checking
        // against Max below is for some reason 10 bytes more code
        telemetryBurstCount = 1;
    }
    else
    {
        // DBGLN("telemetryBurstCount: %d, telemetryBurstMax: %d", telemetryBurstCount, telemetryBurstMax);
        if (telemetryBurstCount < telemetryBurstMax)
        {
            telemetryBurstCount++;
        }
        else
        {
            NextTelemetryType = ELRS_TELEMETRY_TYPE_LINK;
        }

        if (TelemetrySender.IsActive())
        {
            if (OtaIsFullRes)
            {
                otaPkt.full.tlm_dl.packageIndex = TelemetrySender.GetCurrentPayload(otaPkt.full.tlm_dl.payload, sizeof(otaPkt.full.tlm_dl.payload));
            }
            else
            {
                otaPkt.std.tlm_dl.type = ELRS_TELEMETRY_TYPE_DATA;
                otaPkt.std.tlm_dl.packageIndex = TelemetrySender.GetCurrentPayload(otaPkt.std.tlm_dl.payload, sizeof(otaPkt.std.tlm_dl.payload));
            }
        }
    }

    OtaGeneratePacketCrc(&otaPkt);

    Tk86xxSendData((uint8_t *)&otaPkt, ExpressLRS_currAirRate_Modparams->PayloadLength);

    return true;
}

static void TXdoneISR(void)
{
    HandleSendTelemetryResponse();
}

static void ClearPowerOnCounter(void)
{
    if (/*connectionState != connected &&*/ rxConfig.GetPowerOnCounter() != 0)
    {
        rxConfig.SetPowerOnCounter(0);
        rxConfig.Commit();
        DBGLN("Cleared power on counter");
    }
}
static void setupConfigAndPocCheck(void)
{
    ELRS_EEPROM_Init(&eeprom);
    RxConfig_Init(&rxConfig);
    rxConfig.SetStorageProvider(&eeprom); // Pass pointer to the Config class for access to storage
    #ifndef SIM_TUBE
    rxConfig.Load();
    #endif

    // // If bound, track number of plug/unplug cycles to go to binding mode in eeprom
    if (rxConfig.GetIsBound() && rxConfig.GetPowerOnCounter() < 3)
    {
        rxConfig.SetPowerOnCounter(rxConfig.GetPowerOnCounter() + 1);
        rxConfig.Commit();
        DBGLN("Power on counter = %u", rxConfig.GetPowerOnCounter());
    }

    // // Set a deferred function to clear the power on counter if the RX has been running for more than 2s
    deferExecutionMillis(2000, ClearPowerOnCounter);
}

static void EnterBindingMode()
{
    if (InBindingMode)
    {
        DBGLN("Already in binding mode");
        return;
    }

    // never enter binding mode if binding is supposed to only be administered through the web UI
    if (rxConfig.GetBindStorage() == BINDSTORAGE_ADMINISTERED) {
        return;
    }

    // Binding uses a CRCInit=0, 50Hz, and InvertIQ
    OtaCrcInitializer = 0;
    OtaNonce = 0;
    FHSSsetCurrIndex(0);
    MspReceiver.ResetState();
    connectionState = disconnected;
    connectionHasModelMatch = false;
    InBindingMode = true;
    // Any method of entering bind resets a loan
    // Model can be reloaned immediately by binding now
    rxConfig.ReturnLoan();
    rxConfig.Commit();

    // Start attempting to bind
    // Lock the RF rate and freq while binding
    SetRFLinkRate(RATE_BINDING, true);
    ExpressLRS_currTlmDenom = TLMratioEnumToValue(ExpressLRS_currAirRate_Modparams->TLMinterval);

    // If the Radio Params (including InvertIQ) parameter changed, need to restart RX to take effect
    // Radio.RXnb();

    DBGLN("Entered binding mode at freq = %d", FHSSgetInitialFreq());
    devicesTriggerEvent();
}

void EnterRxBindingModeSafely(void)
{
    // Will not enter Binding mode if in the process of a passthrough update
    // or currently binding
    if (connectionState == serialUpdate || InBindingMode)
        return;

//     // If connected, handle that in updateBindingMode()
//     if (connectionState == connected)
//     {
//         BindingModeRequest = true;
//         return;
//     }

    EnterBindingMode();
}

static void setupBindingFromConfig()
{
    // VolatileBind's only function is to prevent loading the stored UID into RAM
    // which makes the RX boot into bind mode every time
    if (rxConfig.GetIsBound())
    {
        memcpy(UID, rxConfig.GetUID(), UID_LEN);
    }

    DBGLN("UID=(%d, %d, %d, %d, %d, %d) ModelId=%u",
        UID[0], UID[1], UID[2], UID[3], UID[4], UID[5], rxConfig.GetModelId());

    OtaUpdateCrcInitFromUid();
}

static void setupSerial()
{
    eSerialProtocol_e proto = PROTOCOL_CRSF;
    if (rxConfig.GetSerialProtocol)
    {
        proto = rxConfig.GetSerialProtocol();
    }

#if defined(USE_SBUS_PROTOCOL)
    // This tk8620_soc port does not currently expose a runtime UI to change serialProtocol,
    // so allow the existing ExpressLRS-style build-time selection to force SBUS output.
    proto = PROTOCOL_SBUS;
#endif

    uint32_t baud = firmwareOptions.uart_baud ? firmwareOptions.uart_baud : 420000;
    Tk86xxSerialConfig serialConfig = {
        .baudRate = baud,
        .wordLength = TK86XX_SERIAL_WORD_LENGTH_8B,
        .parity = TK86XX_SERIAL_PARITY_NONE,
        .stopBits = TK86XX_SERIAL_STOP_BITS_1,
        .duplex = TK86XX_SERIAL_DUPLEX_FULL,
    };

    switch (proto)
    {
    case PROTOCOL_SBUS:
    case PROTOCOL_INVERTED_SBUS:
        serialConfig.baudRate = 100000;
        serialConfig.parity = TK86XX_SERIAL_PARITY_EVEN;
        serialConfig.stopBits = TK86XX_SERIAL_STOP_BITS_2;
        break;
    default:
        break;
    }

    Tk86xxSerialInit(&serialConfig);
    SerialIO_Init(&serialIO);
    SerialIO_SetProtocol(&serialIO, proto);
}

static void SetRFLinkRate(uint8_t index, bool bindMode) // Set speed of RF link
{
    DBGLN("SetRFLinkRate: index = %u", index);
    expresslrs_mod_settings_t *const ModParams = get_elrs_airRateConfig(index);
    expresslrs_rf_pref_params_s *const RFperf = get_elrs_RFperfParams(index);

    OtaUpdateSerializers(smWideOr8ch, ModParams->PayloadLength);
    MspReceiver.setMaxPackageIndex(ELRS_MSP_MAX_PACKAGES);
    TelemetrySender.setMaxPackageIndex(OtaIsFullRes ? ELRS8_TELEMETRY_MAX_PACKAGES : ELRS4_TELEMETRY_MAX_PACKAGES);

    ExpressLRS_currAirRate_Modparams = ModParams;
    ExpressLRS_nextAirRateIndex = index;
    ExpressLRS_currAirRate_RFperfParams = RFperf;
    telemBurstValid = false;
    UplinkLqTracker_Reset(bindMode ? UPLINK_LQ_HISTORY_SIZE : UplinkLqTracker_ComputeWindowSize(ModParams));
    CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_Link_quality = 0;
}

static void setupRadio()
{
    DevRadioRx_RegisterRxSlotResultCb(RXdoneISR);
    DevRadioRx_RegisterTxDoneCb(TXdoneISR);

    scanIndex = rxConfig.GetRateInitialIdx();
    DBGLN("scanIndex = %u", scanIndex);
    SetRFLinkRate(scanIndex, false);
    ExpressLRS_currTlmDenom  = TLMratioEnumToValue(ExpressLRS_currAirRate_Modparams->TLMinterval);
    // Start slow on the selected rate to give it the best chance
    // to connect before beginning rate cycling
    // RFmodeCycleMultiplier = RFmodeCycleMultiplierSlow / 2;
}

static void setup(void)
{
    wdt_init_t    wdt_param           = {.mode = RESET_MODE, .count = 6000};

    hardwareConfigured = options_init();

    if (hardwareConfigured) {
        // default to CRSF protocol and the compiled baud rate
        // serialBaud = firmwareOptions.uart_baud;
        Tk86xxSerialConfig serialConfig = {
            .baudRate = 420000,
            .wordLength = TK86XX_SERIAL_WORD_LENGTH_8B,
            .parity = TK86XX_SERIAL_PARITY_NONE,
            .stopBits = TK86XX_SERIAL_STOP_BITS_1,
            .duplex = TK86XX_SERIAL_DUPLEX_FULL,
        };
        Tk86xxSerialInit(&serialConfig);
        DBGLN("ELRS RX %s", firmware_build_id);
#if SENSI_TEST
#if SENSI_TEST_PROFILE
        DBGLN("SENSI RX module-pair");
#else
        DBGLN("SENSI RX signal-generator");
#endif
#endif

        // // pre-initialise serial must be done before anything as some libs write
        // // to the serial port and they'll block if the buffer fills
        // #if defined(DEBUG_LOG)
        // Serial.begin(serialBaud);
        // SerialLogger = &Serial;
        // #else
        // SerialLogger = new NullStream();
        // #endif

        // External EEPROM needs I2C setup so it can load config
        // but configurable I2C pins for PWM RX needs config loaded first

        // Init EEPROM and load config, checking powerup count
        setupConfigAndPocCheck();

        // #if defined(OPT_HAS_SERVO_OUTPUT)
        // // If serial is not already defined, then see if there is serial pin configured in the PWM configuration
        // if (GPIO_PIN_RCSIGNAL_RX == UNDEF_PIN && GPIO_PIN_RCSIGNAL_TX == UNDEF_PIN)
        // {
        //     for (int i = 0 ; i < GPIO_PIN_PWM_OUTPUTS_COUNT ; i++)
        //     {
        //         eServoOutputMode pinMode = (eServoOutputMode)config.GetPwmChannel(i)->val.mode;
        //         if (pinMode == somSerial)
        //         {
        //             pwmSerialDefined = true;
        //             break;
        //         }
        //     }
        // }
        // #endif
        setupSerial();
        devicesRegister(ui_devices, ARRAY_SIZE(ui_devices));
        Telemetry_Init(&telemetry);
        telemetry.ResetState();
        StubbornSender_Init(&TelemetrySender);
        StubbornReceiver_Init(&MspReceiver);
        setupBindingFromConfig();
        FHSSrandomiseFHSSsequence(uidMacSeedGet()); 
        GENERIC_CRC8Init(CRSF_CRC_POLY);
        setupRadio();

        // Power management + optional MatchTX behaviour (needs OTA uplinkPower decode).
        POWERMGNT_init();
        DynamicPower_UpdateRx(true);
        devicesInit();

        if (connectionState != radioFailed)
        {
            MspReceiver.SetDataToReceive(MspData, ELRS_MSP_BUFFER);
            // Radio.RXnb();
            // hwTimer::init(HWtimerCallbackTick, HWtimerCallbackTock);
        }
    }

// #if defined(HAS_BUTTON)
    registerButtonFunction(ACTION_BIND, EnterRxBindingModeSafely);
//     registerButtonFunction(ACTION_RESET_REBOOT, resetConfigAndReboot);
// #endif

    connectionState = disconnected;
    devicesTriggerEvent();
    devicesStart();

    // setup() eats up some of this time, which can cause the first mode connection to fail.
    // Resetting the time here give the first mode a better chance of connection.
    // RFmodeLastCycled = millis();
    eclic_priority_group_set(ECLIC_PRIGROUP_LEVEL3_PRIO0); // 10us
    wdt_init(&wdt_param); // 7us
    wdt_enable();
}

static void LostConnection(bool resumeRx)
{
    DBGLN("lost conn");

    connectionState = disconnected; //set lost connection
    UplinkLqTracker_Reset(s_uplinkLqTracker.windowSize);
    CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_Link_quality = 0;
    // RXtimerState = tim_disconnected;
    // hwTimer::resetFreqOffset();
    // PfdPrevRawOffset = 0;
    // GotConnectionMillis = 0;
    // uplinkLQ = 0;
    // LQCalc.reset();
    // LQCalcDVDA.reset();
    // LPF_Offset.init(0);
    // LPF_OffsetDx.init(0);
    // alreadyTLMresp = false;
    // alreadyFHSS = false;

    if (!InBindingMode)
    {
        // if (hwTimer::running)
        // {
        //     while(micros() - PFDloop.getIntEventTime() > 250); // time it just after the tock()
        //     hwTimer::stop();
        // }
        SetRFLinkRate(ExpressLRS_nextAirRateIndex, false); // also sets to initialFreq
        // If not resumRx, Radio will be left in SX127x_OPMODE_STANDBY / SX1280_MODE_STDBY_XOSC
        // if (resumeRx)
        // {
        //     Radio.RXnb();
        // }
    }
}

static void ExitBindingMode()
{
    if (!InBindingMode)
    {
        DBGLN("Not in binding mode");
        return;
    }

    MspReceiver.ResetState();

    // // Prevent any new packets from coming in
    // Radio.SetTxIdleMode();
    // Write the values to eeprom
    rxConfig.Commit();

    OtaUpdateCrcInitFromUid();
    FHSSrandomiseFHSSsequence(uidMacSeedGet());
    FHSSsetCurrIndex(0);
    // Force RF cycling to start at the beginning immediately
    // scanIndex = RATE_MAX;
    // RFmodeLastCycled = 0;
    // LockRFmode = false;
    // LostConnection(false);

    // Do this last as LostConnection() will wait for a tock that never comes
    // if we're in binding mode
    InBindingMode = false;
    SetRFLinkRate(rxConfig.GetRateInitialIdx(), false);
    ExpressLRS_currTlmDenom = TLMratioEnumToValue(ExpressLRS_currAirRate_Modparams->TLMinterval);
    DBGLN("Exiting binding mode");
    devicesTriggerEvent();
}

static void updateBindingMode(unsigned long now)
{
    // Exit binding mode if the config has been modified, indicating UID has been set
    if (InBindingMode && rxConfig.IsModified())
    {
        ExitBindingMode();
    }
    // If the power on counter is >=3, enter binding, the counter will be reset after 2s
    else if (!InBindingMode && rxConfig.GetPowerOnCounter() >= 3)
    {
        DBGLN("Power on counter >=3, enter binding mode");
        EnterBindingMode();
    }
    // If the eeprom is indicating that we're not bound, enter binding
    else if (!UID_IS_BOUND(UID) && !InBindingMode)
    {
        #if !SENSI_TEST
        DBGLN("RX has not been bound, enter binding mode");
        EnterBindingMode();
        #endif
    }
    else if (BindingModeRequest)
    {
        // DBGLN("Connected request to enter binding mode");
        // BindingModeRequest = false;
        // if (connectionState == connected)
        // {
        //     LostConnection(false);
        //     // Skip entering bind mode if on loan. This comes from an OTA request
        //     // and the model is assumed to be inaccessible, do not want the receiver
        //     // sitting in a field ready to be bound to anyone within 10km
        //     if (config.IsOnLoan())
        //     {
        //         DBGLN("Model was on loan, becoming inert");
        //         config.ReturnLoan();
        //         config.Commit(); // prevents CheckConfigChangePending() re-enabling radio
        //         Radio.End();
        //         // Enter a completely invalid state for a receiver, to prevent wifi or radio enabling
        //         connectionState = noCrossfire;
        //         return;
        //     }
        //     // if the InitRate config item was changed by LostConnection
        //     // save the config before entering bind, as the modified config
        //     // will immediately boot it out of bind mode
        //     config.Commit();
        // }
        // EnterBindingMode();
    }
}

static void updateTelemetryBurst()
{
    if (telemBurstValid)
        return;
    telemBurstValid = true;

    uint16_t hz = 1000000 / ExpressLRS_currAirRate_Modparams->interval;
    telemetryBurstMax = TLMBurstMaxForRateRatio(hz, ExpressLRS_currTlmDenom);

    // Notify the sender to adjust its expected throughput
    TelemetrySender.UpdateTelemetryRate(hz, ExpressLRS_currTlmDenom, telemetryBurstMax);
}

void UpdateModelMatch(uint8_t model)
{
    rxConfig.SetModelId(model);
}

static bool HandleRxConfigMspWrite(const uint8_t totalLen)
{
    if (totalLen < 10U)
    {
        return false;
    }

    if (MspData[CRSF_TELEMETRY_TYPE_INDEX] != CRSF_FRAMETYPE_MSP_WRITE ||
        MspData[7] != MSP_SET_RX_CONFIG ||
        MspData[8] != MSP_ELRS_MODEL_ID)
    {
        return false;
    }

    UpdateModelMatch(MspData[9]);
    return true;
}

/**
 * Process the assembled MSP packet in MspData[]
 **/
static void MspReceiveComplete()
{
    const uint8_t totalLen = CRSF_FRAME_SIZE(MspData[1]);
    if (totalLen < 4 || totalLen > ELRS_MSP_BUFFER) {
        MspReceiver.Unlock();
        return;
    }

    const crsf_header_t *header = (crsf_header_t *)MspData;
    if (header->type >= CRSF_FRAMETYPE_DEVICE_PING)
    {
        const crsf_ext_header_t *receivedHeader = (crsf_ext_header_t *)MspData;

        if (receivedHeader->dest_addr == CRSF_ADDRESS_BROADCAST ||
            receivedHeader->dest_addr == CRSF_ADDRESS_CRSF_RECEIVER)
        {
            if (HandleRxConfigMspWrite(totalLen))
            {
                MspReceiver.Unlock();
                return;
            }
            else if (receivedHeader->type == CRSF_FRAMETYPE_DEVICE_PING)
            {
                uint8_t deviceInformation[DEVICE_INFORMATION_LENGTH];
                CRSF_GetDeviceInformation(deviceInformation, luaGetFieldCount());
                CRSF_SetExtendedHeaderAndCrc(deviceInformation, CRSF_FRAMETYPE_DEVICE_INFO,
                    DEVICE_INFORMATION_FRAME_SIZE, CRSF_ADDRESS_CRSF_RECEIVER, CRSF_ADDRESS_CRSF_TRANSMITTER);
                telemetry.AppendTelemetryPackage(deviceInformation);
            }
            else
            {
                luaParamUpdateReq(
                    MspData[CRSF_TELEMETRY_TYPE_INDEX],
                    MspData[CRSF_TELEMETRY_FIELD_ID_INDEX],
                    MspData[CRSF_TELEMETRY_FIELD_CHUNK_INDEX]
                );
            }
        }

        if (connectionHasModelMatch && teamraceHasModelMatch &&
            (receivedHeader->dest_addr == CRSF_ADDRESS_BROADCAST ||
             receivedHeader->dest_addr == CRSF_ADDRESS_FLIGHT_CONTROLLER))
        {
            serialIO.queueMSPFrameTransmission(MspData);
        }
    }

    MspReceiver.Unlock();
}

static void CheckConfigChangePending()
{
    if (rxConfig.IsModified() && !InBindingMode && connectionState < NO_CONFIG_SAVE_STATES)
    {
        DBGLN("Config changed, commit");
        // LostConnection(false);
        rxConfig.Commit();
        DevRadioRx_RequestAirRateChange(ExpressLRS_nextAirRateIndex);
        devicesTriggerEvent();
    }
}

static void updateSwitchMode()
{
    // Negative value means waiting for confirm of the new switch mode while connected
    if (SwitchModePending <= 0)
        return;

    // DBGLN("updateSwitchMode: SwitchMode = %d", SwitchModePending - 1);
    OtaUpdateSerializers((OtaSwitchMode_e)(SwitchModePending - 1), ExpressLRS_currAirRate_Modparams->PayloadLength);
    SwitchModePending = 0;
}

static void queueRxVersionTelemetryIfIdle(uint32_t now)
{
    const bool linkReady =
        connectionState == connected &&
        connectionHasModelMatch &&
        teamraceHasModelMatch &&
        ExpressLRS_currTlmDenom != 1U &&
        !InBindingMode;

    if (!linkReady)
    {
        rxVersionPushConnected = false;
        rxVersionPushConnectedMs = 0U;
        rxVersionPushLastMs = 0U;
        return;
    }

    if (!rxVersionPushConnected)
    {
        rxVersionPushConnected = true;
        rxVersionPushConnectedMs = now;
        rxVersionPushLastMs = 0U;
    }

    const uint32_t connectedMs = now - rxVersionPushConnectedMs;
    const uint32_t intervalMs =
        (connectedMs < RX_VERSION_PUSH_FAST_WINDOW_MS) ?
        RX_VERSION_PUSH_FAST_INTERVAL_MS :
        RX_VERSION_PUSH_SLOW_INTERVAL_MS;

    if (rxVersionPushLastMs != 0U && (now - rxVersionPushLastMs) < intervalMs)
    {
        return;
    }

    if (TelemetrySender.IsActive() ||
        size(&telemetry.messagePayloads) != 0U ||
        luaParamUpdatePending())
    {
        return;
    }

    luaParamUpdateReq(CRSF_FRAMETYPE_PARAMETER_READ, RX_LUA_VERSION_FIELD_ID, 0U);
    rxVersionPushLastMs = now;
}

static void loop(void)
{
    unsigned long now = millis();
    devicesUpdate(now);
    // Read and process serial traffic, including queued telemetry payloads.
    handleSerialIO();
    wdt_feed();
    updateBindingMode(now);
    executeDeferredFunction(micros());
    if (connectionState != connectionState_backup)
    {
        connectionState_backup = connectionState;
        devicesTriggerEvent();
    }

    if (MspReceiver.HasFinishedData())
    {
        MspReceiveComplete();
    }
    queueRxVersionTelemetryIfIdle(now);
    luaHandleUpdateParameter();
    CheckConfigChangePending();

    if ((connectionState != disconnected) && (ExpressLRS_currAirRate_Modparams->index != ExpressLRS_nextAirRateIndex))
    {
        DBGLN("Req air rate change %u->%u", ExpressLRS_currAirRate_Modparams->index, ExpressLRS_nextAirRateIndex);
        LostConnection(true);
    }

    uint8_t nextPlayloadSize = 0;
    if (!TelemetrySender.IsActive() && telemetry.GetNextPayload(&nextPlayloadSize, currentTelemetryPayload))
    {
        TelemetrySender.SetDataToTransmit(currentTelemetryPayload, nextPlayloadSize);
    }

    updateTelemetryBurst();
    updateSwitchMode();
    DynamicPower_UpdateRx(false);
}

/**
 * @brief
 * @return int
 */
int main(void)
{
    setup();
    while (1) loop();
}
