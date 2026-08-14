#include <string.h>

#include "tk86xx_api.h"
#include "serial_port.h"
#include "flash_hal.h"
#include "tk86xx_platform.h"
#include "wdt_hal.h"
#include "eclic_hal.h"
#include "FHSS.h"
#include "device.h"
#include "devHandset.h"
#include "devRadioTx.h"
#include "helpers.h"
#include "common.h"
#include "CRSFHandset.h"
#include "OTA.h"
#include "crc.h"
#include "crsf_protocol.h"
#include "elrs_eeprom.h"
#include "config.h"
#include "options.h"
#include "logging.h"
#include "efuse_hal.h"
#include "devButton.h"
#include "src/lib/LUA/devLUA.h"
#include "lua.h"
#include "msptypes.h"
#include "devLED.h"
#include "stubborn_receiver.h"
#include "CRSF.h"
#include "FIFO.h"
#include "POWERMGNT.h"
#include "dynpower.h"
#include "handset.h"
#include "rx_ota_sender.h"
#include "airport.h"
#include "unified_config.h"

#define BindingSpamAmount 25
// Rate changes have no receiver acknowledgement, so cover low-LQ links with a longer SYNC burst.
#define syncSpamAmount 100
#define syncSpamAmountAfterRateChange 10

#if ELRS_HAS_AIRPORT
static device_affinity_t airport_ui_devices[] = {
    {&ratioTxDevice, 0},
    {&LED_device, 0},
    {&Button_device, 1},
};
#endif
#if !ELRS_AIRPORT
static device_affinity_t rc_ui_devices[] = {
    {&Handset_device, 0},
    {&ratioTxDevice, 0},
    {&LED_device, 0}, // LED_device is defined in devHandset.h
    {&LUA_TxDevice, 0},
    {&Button_device, 1},
};
#endif

ELRS_EEPROM_t eeprom;
TxConfig_t txConfig;
uint8_t MSPDataPackage[5];
static StubbornReceiver_t TelemetryReceiver;
StubbornSender_t MspSender;
// Preserve a changed downlink confirm until an uplink RC packet is actually queued.
static volatile bool TelemetryConfirmPending = false;
static volatile uint32_t LastTLMpacketRecvMillis = 0;
static uint8_t CRSFinBuffer[CRSF_MAX_PACKET_LEN+1];
static uint32_t TLMpacketReported = 0;
static volatile uint32_t syncSpamCounter = 0;
static volatile uint8_t syncSpamCounterAfterRateChange = 0;
static volatile bool ModelUpdatePending = false;
static bool commitInProgress = false;
static uint32_t SyncPacketLastSent = 0;
uint32_t rfModeLastChangedMS = 0;
static uint32_t lastLostMillis;
#if !ELRS_AIRPORT
static enum { stbIdle, stbRequested, stbBoosting } syncTelemBoostState = stbIdle;
#endif
static bool DownlinkTlmReceivedThisWindow = false;
static bool TxPayloadPending = false;
static bool NextPacketIsMspData = false;
static bool RxOtaModeRequested = false;
static bool RxOtaModeWasActive = false;
static bool txRebootRequested = false;

#if ELRS_HAS_AIRPORT
static AirportFifo_t AirportUartToRf;
static AirportFifo_t AirportRfToUart;

#if ELRS_UNIFIED
RAMCODE_SECTION static void AirportBufferUartByte(uint8_t byte)
{
  (void)AirportFifo_PushBytes(&AirportUartToRf, &byte, 1U);
}
#endif

RAMCODE_SECTION static void AirportUartRxCallback(uint8_t *data, uint8_t len)
{
#if ELRS_UNIFIED
  UnifiedConfig_FilterBytes(data, len, AirportBufferUartByte);
#else
  AirportFifo_PushBytes(&AirportUartToRf, data, len);
#endif
}

static void AirportFlushUartOutput(void)
{
  uint8_t data[AIRPORT_OTA_MAX_PAYLOAD];
  uint16_t count = AirportFifo_PopBytes(&AirportRfToUart, data, sizeof(data));
  if (count != 0U) {
    (void)Tk86xxSerialWrite(data, count);
  }
}
#endif

static void UnifiedStopNormalOperation(void)
{
  UnifiedConfig_SetLoggingEnabled(false);
  DevRadioTx_Stop();
  TxPayloadPending = false;
  connectionState = serialUpdate;
  if (LED_device.event != NULL) (void)LED_device.event();
}

static void UnifiedRequestReboot(void)
{
  txRebootRequested = true;
}

static void SetRFLinkRate(uint8_t index);

static void UpdateLuaDuringRxOta(void)
{
  if (handset.handleInput != NULL)
  {
    (void)handset.handleInput();
  }
  (void)luaHandleUpdateParameter();
  CRSFHandset_FlushOutput();
}

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

#define DOWNLINK_LQ_WINDOW_SIZE 25U

typedef struct {
  uint8_t lq;
  uint8_t index;
  uint8_t count;
  uint32_t mask;
  uint32_t bits[(DOWNLINK_LQ_WINDOW_SIZE + 31U) / 32U];
} DownlinkLqCalc_t;

static DownlinkLqCalc_t DownlinkLqCalc = {
  .count = 1,
  .mask = 1U,
};

static bool DownlinkLqCurrentIsSet(void)
{
  return (DownlinkLqCalc.bits[DownlinkLqCalc.index] & DownlinkLqCalc.mask) != 0U;
}

static void DownlinkLqAdd(void)
{
  if (DownlinkLqCurrentIsSet())
    return;

  DownlinkLqCalc.bits[DownlinkLqCalc.index] |= DownlinkLqCalc.mask;
  DownlinkLqCalc.lq++;
}

static uint8_t DownlinkLqGet(void)
{
  return (uint8_t)(((uint32_t)DownlinkLqCalc.lq * 100U) / DownlinkLqCalc.count);
}

static void DownlinkLqInc(void)
{
  DownlinkLqCalc.mask <<= 1;
  if (DownlinkLqCalc.mask == 0U)
  {
    DownlinkLqCalc.mask = 1U;
    DownlinkLqCalc.index++;
  }

  if ((DownlinkLqCalc.index == (DOWNLINK_LQ_WINDOW_SIZE / 32U)) &&
      ((DownlinkLqCalc.mask & (1U << (DOWNLINK_LQ_WINDOW_SIZE % 32U))) != 0U))
  {
    DownlinkLqCalc.index = 0;
    DownlinkLqCalc.mask = 1U;
  }

  if ((DownlinkLqCalc.bits[DownlinkLqCalc.index] & DownlinkLqCalc.mask) != 0U)
  {
    DownlinkLqCalc.bits[DownlinkLqCalc.index] &= ~DownlinkLqCalc.mask;
    DownlinkLqCalc.lq--;
  }

  if (DownlinkLqCalc.count < DOWNLINK_LQ_WINDOW_SIZE)
  {
    DownlinkLqCalc.count++;
  }
}

static bool setupHardwareFromOptions(void)
{
  options_init();
  return true;
}

static void setupBindingFromConfig()
{
    chip_sn_info_t sn_info;

    DBGLN("hasUID: %d", firmwareOptions.hasUID);
    if (firmwareOptions.hasUID) {
        memcpy(UID, firmwareOptions.uid, UID_LEN);
    } else {
        efuse_read_sn_info(&sn_info);
        // DBGLN("chip id:0x%08x", sn_info.chip_id);
#if !SENSI_TEST
        UID[0] = 0;
        UID[1] = 0;
        UID[2] = (uint8_t)(sn_info.chip_id >> 24);
        UID[3] = (uint8_t)(sn_info.chip_id >> 16);
        UID[4] = (uint8_t)(sn_info.chip_id >> 8);
        UID[5] = (uint8_t)(sn_info.chip_id);
#endif
    }

    #ifdef SIM_TUBE
    UID[0] = 0;
    UID[1] = 0;
    UID[2] = 0;
    UID[3] = 0;
    UID[4] = 0;
    UID[5] = 1;
    #endif
    DBGLN("UID=(%02x, %02x, %02x, %02x, %02x, %02x)", UID[0], UID[1], UID[2], UID[3], UID[4], UID[5]);

    OtaUpdateCrcInitFromUid();
}

static void SendUIDOverMSP()
{
    MSPDataPackage[0] = MSP_ELRS_BIND;
    memcpy(&MSPDataPackage[1], &UID[2], 4);
    BindingSendCount = 0;

    MspSender.ResetState();
    MspSender.SetDataToTransmit(MSPDataPackage, 5);
}

static void EnterBindingMode()
{
    if (InBindingMode)
        return;

  // Disable the TX timer and wait for any TX to complete
//   hwTimer::stop();
//   while (busyTransmitting);

    // Queue up sending the Master UID as MSP packets
    SendUIDOverMSP();

  // Binding uses a CRCInit=0, 50Hz, and InvertIQ
  OtaCrcInitializer = 0;
  OtaNonce = 0; // Lock the OtaNonce to prevent syncspam packets
  FHSSsetCurrIndex(0);
  InBindingMode = true; // Set binding mode before SetRFLinkRate() for correct IQ

  // Start attempting to bind
  // Lock the RF rate and freq while binding
  SetRFLinkRate(RATE_BINDING);
  ExpressLRS_currTlmDenom = TLMratioEnumToValue(ExpressLRS_currAirRate_Modparams->TLMinterval);

  // Start transmitting again
//   hwTimer::resume();

  DBGLN("Entered binding mode at freq = %d", FHSSgetInitialFreq());
  devicesTriggerEvent();
}

void EnterBindingModeSafely(void)
{
    // TX can always enter binding mode safely as the function handles stopping the transmitter
    EnterBindingMode();
}

void StartRxOtaModeSafely(void)
{
  RxOtaModeRequested = true;
}

void SetSyncSpam()
{
  // Send sync spam if a UI device has requested to and the config has changed
  if (txConfig.IsModified())
  {
    syncSpamCounter = syncSpamAmount;
    syncSpamCounterAfterRateChange = syncSpamAmountAfterRateChange;
  }
}

static void SetRFLinkRate(uint8_t index) // Set speed of RF link
{
    expresslrs_mod_settings_t *const ModParams = get_elrs_airRateConfig(index);
      expresslrs_rf_pref_params_s *const RFperf = get_elrs_RFperfParams(index);
    // Binding always uses invertIQ
    // bool invertIQ = InBindingMode || (UID[5] & 0x01);
    OtaSwitchMode_e newSwitchMode = (OtaSwitchMode_e)txConfig.GetSwitchMode();
    // DBGLN("newSwitchMode = %u", newSwitchMode);

  if ((ModParams == ExpressLRS_currAirRate_Modparams)
    && (RFperf == ExpressLRS_currAirRate_RFperfParams)
    && (OtaSwitchModeCurrent == newSwitchMode))
    return;

    DBGLN("set rate %u", index);
//   uint32_t interval = ModParams->interval;

  // InitialFreq has been set, so lets also reset the FHSS Idx and Nonce.
  FHSSsetCurrIndex(0);
  OtaNonce = 0;

  OtaUpdateSerializers(newSwitchMode, ModParams->PayloadLength);
  MspSender.setMaxPackageIndex(ELRS_MSP_MAX_PACKAGES);
  TelemetryReceiver.setMaxPackageIndex(OtaIsFullRes ? ELRS8_TELEMETRY_MAX_PACKAGES : ELRS4_TELEMETRY_MAX_PACKAGES);

  ExpressLRS_currAirRate_Modparams = ModParams;
  ExpressLRS_currAirRate_RFperfParams = RFperf;
  CRSF_GetLinkStatistics()->crsfLinkStatistics.rf_Mode = ModParams->enum_rate;

//   handset->setPacketInterval(interval * ExpressLRS_currAirRate_Modparams->numOfSends);
  connectionState = disconnected;
  rfModeLastChangedMS = millis();
}

static void ChangeRadioParams()
{
  ModelUpdatePending = false;
  // DBGLN("txConfig.GetRate() = %u", txConfig.GetRate());
  SetRFLinkRate(txConfig.GetRate());
  ExpressLRS_currTlmDenom = TLMratioEnumToValue(ExpressLRS_currAirRate_Modparams->TLMinterval);
  txLostSignal = true;
  devicesTriggerEvent();
}

static void LinkStatsFromOta(OTA_LinkStats_s * const ls)
{
  DynamicPower_TelemetryUpdate(ls->SNR);
  // DBGLN("LinkStatsFromOta: %d,%d", ls->SNR, ls->uplink_RSSI_1);

  // RSSI received over OTA is a positive -dBm magnitude. Store the CRSF handset
  // value as signed dBm, matching upstream ExpressLRS behavior.
  CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_RSSI_1 = (uint8_t)(-(int8_t)ls->uplink_RSSI_1);
  CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_RSSI_2 = (uint8_t)(-(int8_t)ls->uplink_RSSI_2);
  CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_Link_quality = ls->lq;
#if defined(DEBUG_FREQ_CORRECTION)
  // Don't descale the FreqCorrection value being send in SNR
  CRSF::LinkStatistics.uplink_SNR = snrScaled;
#else
  CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_SNR = ls->SNR;
#endif
  CRSF_GetLinkStatistics()->crsfLinkStatistics.active_antenna = ls->antenna;
  connectionHasModelMatch = ls->modelMatch;
  // -- downlink_SNR / downlink_RSSI is updated for any packet received, not just Linkstats
  // -- uplink_TX_Power is updated when sending to the handset, so it updates when missing telemetry
  // -- rf_mode is updated when we change rates
  // -- downlink_Link_quality is updated before the LQ period is incremented
  MspSender.ConfirmCurrentPayload(ls->mspConfirm);
}

static bool ProcessTLMpacket(uint8_t *data, uint16_t data_len, SignalQuality_t *signalQuality)
{
  const uint32_t now = millis();
#if !ELRS_AIRPORT
  const bool telemetryConfirmBefore = TelemetryReceiver.GetCurrentConfirm();
#endif
#if SENSI_TEST
  LastTLMpacketRecvMillis = now;
  lastLostMillis = now + 10 * 1000;
  return true;
#endif

  if (data_len < ExpressLRS_currAirRate_Modparams->PayloadLength)
  {
    return false;
  }

  OTA_Packet_s * const otaPktPtr = (OTA_Packet_s * const)data;
  if (!OtaValidatePacketCrc(otaPktPtr))
  {
    // DBGLN("TLM crc error");
    return false;
  }

  if (otaPktPtr->std.type != PACKET_TYPE_TLM)
  {
    DBGLN("TLM type error %d", otaPktPtr->std.type);
    return false;
  }
  // DBGLN("recv TLM packet");
  LastTLMpacketRecvMillis = millis();
  lastLostMillis = now + 10 * 1000;
  DownlinkTlmReceivedThisWindow = true;

  CRSF_GetLinkStatistics()->crsfLinkStatistics.downlink_SNR = signalQuality->snr;
  CRSF_GetLinkStatistics()->crsfLinkStatistics.downlink_RSSI_1 = signalQuality->rssi;
  CRSF_GetLinkStatistics()->downlink_RSSI_2 = 0;

#if ELRS_HAS_AIRPORT
  if (UnifiedConfig_IsAirport()) {
    if (otaPktPtr->full.tlm_dl.containsLinkStats) {
      LinkStatsFromOta(&otaPktPtr->full.tlm_dl.ul_link_stats.stats);
    } else {
      (void)OtaUnpackAirportData(otaPktPtr, &AirportRfToUart);
    }
    return true;
  }
#endif
#if !ELRS_AIRPORT
  // Full res mode
  if (OtaIsFullRes)
  {
    OTA_Packet8_s * const ota8 = (OTA_Packet8_s * const)otaPktPtr;
    uint8_t *telemPtr;
    uint8_t dataLen;
    if (ota8->tlm_dl.containsLinkStats)
    {
      LinkStatsFromOta(&ota8->tlm_dl.ul_link_stats.stats);
      telemPtr = ota8->tlm_dl.ul_link_stats.payload;
      dataLen = sizeof(ota8->tlm_dl.ul_link_stats.payload);
    }
    else
    {
      telemPtr = ota8->tlm_dl.payload;
      dataLen = sizeof(ota8->tlm_dl.payload);
    }
    //DBGLN("pi=%u len=%u", ota8->tlm_dl.packageIndex, dataLen);
    TelemetryReceiver.ReceiveData(ota8->tlm_dl.packageIndex & ELRS8_TELEMETRY_MAX_PACKAGES, telemPtr, dataLen);
  }
  // Std res mode
  else
  {
    switch (otaPktPtr->std.tlm_dl.type)
    {
      case ELRS_TELEMETRY_TYPE_LINK:
        LinkStatsFromOta(&otaPktPtr->std.tlm_dl.ul_link_stats.stats);
        break;

      case ELRS_TELEMETRY_TYPE_DATA:
        TelemetryReceiver.ReceiveData(otaPktPtr->std.tlm_dl.packageIndex & ELRS4_TELEMETRY_MAX_PACKAGES,
          otaPktPtr->std.tlm_dl.payload,
          sizeof(otaPktPtr->std.tlm_dl.payload));
        break;
    }
  }

  if (TelemetryReceiver.GetCurrentConfirm() != telemetryConfirmBefore)
  {
    TelemetryConfirmPending = true;
  }
#endif

  return true;
}

static void RXdoneISR(uint8_t *data, uint16_t data_len, SignalQuality_t *signalQuality)
{
#if SENSI_TEST
  PrintSensiStats(signalQuality, data_len > 0);
  return;
#endif
  // busyTransmitting is required here to prevent accidental rxdone IRQs due to interference triggering RXdoneISR.
  // if (LQCalc.currentIsSet() || busyTransmitting)
  // {
  //   return false; // Already received tlm, do not run ProcessTLMpacket() again.
  // }

  bool packetSuccessful = ProcessTLMpacket(data, data_len, signalQuality);
  UNUSED(packetSuccessful);
#if defined(Regulatory_Domain_EU_CE_2400)
  if (packetSuccessful)
  {
    SetClearChannelAssessmentTime();
  }
#endif

  // return packetSuccessful;
}

static void DownlinkTlmWindowDone(void)
{
  if (DownlinkTlmReceivedThisWindow)
  {
    DownlinkLqAdd();
  }

  CRSF_GetLinkStatistics()->crsfLinkStatistics.downlink_Link_quality = DownlinkLqGet();
  DownlinkLqInc();
  DownlinkTlmReceivedThisWindow = false;
}

static void ClearTxPayloadPending(void)
{
  TxPayloadPending = false;
}

static void TXdoneISR(void)
{
//   if (!busyTransmitting)
//   {
//     return; // Already finished transmission and do not call HandleFHSS() a second time, which may hop the frequency!
//   }
  ClearTxPayloadPending();

//   if (connectionState != awaitingModelId)
//   {
//     HandleFHSS();
//     HandlePrepareForTLM();
// #if defined(Regulatory_Domain_EU_CE_2400)
//     if (TelemetryRcvPhase != ttrpPreReceiveGap)
//     {
//       // Start RX for Listen Before Talk early because it takes about 100us
//       // from RX enable to valid instant RSSI values are returned.
//       // If rx was already started by TLM prepare above, this call will let RX
//       // continue as normal.
//       SetClearChannelAssessmentTime();
//     }
// #endif // non-CE
//   }
//   busyTransmitting = false;
}

static expresslrs_tlm_ratio_e UpdateTlmRatioEffective()
{
#if ELRS_HAS_AIRPORT
  if (UnifiedConfig_IsAirport()) {
    ExpressLRS_currTlmDenom = TLMratioEnumToValue(TLM_RATIO_1_2);
    return TLM_RATIO_1_2;
  }
#endif
#if !ELRS_AIRPORT
  expresslrs_tlm_ratio_e ratioConfigured = (expresslrs_tlm_ratio_e)txConfig.GetTlm();
  // DBGLN("ratioConfigured = %u", ratioConfigured);
  // default is suggested rate for TLM_RATIO_STD/TLM_RATIO_DISARMED
  expresslrs_tlm_ratio_e retVal = ExpressLRS_currAirRate_Modparams->TLMinterval;
  bool updateTelemDenom = true;

  // TLM ratio is boosted until there is one complete sync cycle with no BoostRequest
  // if (syncTelemBoostState == stbBoosting)
  // {
  //   syncTelemBoostState = stbIdle;
  // }

  if (syncTelemBoostState == stbRequested)
  {
    syncTelemBoostState = stbBoosting;
    // default to 1:2 telemetry ratio bump for non-wide modes and
    // wide mode configured to 1:4
    retVal = TLM_RATIO_1_2;

    if (!OtaIsFullRes && txConfig.GetSwitchMode() == smWideOr8ch)
    {
      // avoid crossing the wide switch 7-bit to 6-bit boundary
      if (ratioConfigured <= TLM_RATIO_1_8 || ratioConfigured == TLM_RATIO_DISARMED)
      {
        retVal = TLM_RATIO_1_8;
      }
    }
  }
  // If Armed, telemetry is disabled, otherwise use STD
  else if (ratioConfigured == TLM_RATIO_DISARMED)
  {
    const bool armed = (handset.IsArmed != NULL) ? handset.IsArmed() : false;
    if (armed)
    {
      retVal = TLM_RATIO_NO_TLM;
      // Avoid updating ExpressLRS_currTlmDenom until connectionState == disconnected
      if (connectionState == connected)
        updateTelemDenom = false;
    }
  }
  else if (ratioConfigured != TLM_RATIO_STD)
  {
    retVal = ratioConfigured;
  }

  if (updateTelemDenom)
  {
    uint8_t newTlmDenom = TLMratioEnumToValue(retVal);
    // Delay going into disconnected state when the TLM ratio increases
    if (connectionState == connected && ExpressLRS_currTlmDenom > newTlmDenom)
      LastTLMpacketRecvMillis = SyncPacketLastSent;

    if (ExpressLRS_currTlmDenom != newTlmDenom) {
      const uint8_t previousTlmDenom = ExpressLRS_currTlmDenom;
      ExpressLRS_currTlmDenom = newTlmDenom;
      DevRadioTx_RequestTlmRatioChange(previousTlmDenom);
      devicesTriggerEvent();
    }
  }

  return retVal;
#else
  return TLM_RATIO_1_2;
#endif
}

static void GenerateSyncPacketData(OTA_Sync_s * const syncPtr)
{
  const bool airportMode = UnifiedConfig_IsAirport();
  const uint8_t SwitchEncMode = airportMode ? smWideOr8ch : txConfig.GetSwitchMode();
  const uint8_t Index = airportMode ? ExpressLRS_currAirRate_Modparams->index :
    ((syncSpamCounter) ? txConfig.GetRate() : ExpressLRS_currAirRate_Modparams->index);

  if (syncSpamCounter)
    --syncSpamCounter;

  if (syncSpamCounterAfterRateChange && Index == ExpressLRS_currAirRate_Modparams->index)
  {
    --syncSpamCounterAfterRateChange;
    if (connectionState == connected) // We are connected again after a rate change.  No need to keep spaming sync.
      syncSpamCounterAfterRateChange = 0;
  }

  SyncPacketLastSent = millis();

  expresslrs_tlm_ratio_e newTlmRatio = UpdateTlmRatioEffective();

  syncPtr->fhssIndex = FHSSgetCurrIndex();
  syncPtr->nonce = OtaNonce;
  syncPtr->rateIndex = Index;
  syncPtr->newTlmRatio = newTlmRatio - TLM_RATIO_NO_TLM;
  syncPtr->switchEncMode = SwitchEncMode;
  syncPtr->UID3 = UID[3];
  syncPtr->UID4 = UID[4];
  syncPtr->UID5 = UID[5];

  // For model match, the last byte of the binding ID is XORed with the inverse of the modelId
  if (!InBindingMode && txConfig.GetModelMatch())
  {
    syncPtr->UID5 ^= (uint8_t)((~txConfig.m_modelId) & MODELMATCH_MASK);
  }
}

static void QueueTxPayload(uint8_t *payload)
{
    if (payload == NULL || TxPayloadPending)
        return;

    if (Tk86xxSendData(payload, ExpressLRS_currAirRate_Modparams->PayloadLength) == API_SUCCESS)
    {
        TxPayloadPending = true;
    }
}

static void SendRCdataToRF(bool isRcData)
{
    if (RxOtaSender_IsActive() || RxOtaModeRequested)
        return;

    if (TxPayloadPending)
        return;

    // Do not send a stale channels packet to the RX if one has not been received from the handset
    // *Do* send data if a packet has never been received from handset and the timer is running
    // this is the case when bench testing and TXing without a handset
    //   bool dontSendChannelData = false;
//   uint32_t lastRcData = handset->GetRCdataLastRecv();
//   if (lastRcData && (micros() - lastRcData > 1000000))
//   {
//     // The tx is in Mavlink mode and without a valid crsf or RC input.  Do not send stale or fake zero packet RC!
//     // Only send sync and MSP packets.
//     return;
//   }

//   busyTransmitting = true;

      uint32_t const now = millis();
    // ESP requires word aligned buffer
    WORD_ALIGNED_ATTR OTA_Packet_s otaPkt = {0};
    uint8_t *p = NULL;
    static uint32_t syncPacketCount = 0;
    //   static uint8_t syncSlot;

  const bool airportMode = UnifiedConfig_IsAirport();
  const bool isTlmDisarmed = airportMode ? false : (txConfig.GetTlm() == TLM_RATIO_DISARMED);
  uint32_t SyncInterval = (connectionState == connected && !isTlmDisarmed) ? ExpressLRS_currAirRate_RFperfParams->SyncPktIntervalConnected : ExpressLRS_currAirRate_RFperfParams->SyncPktIntervalDisconnected;
  const bool armed = airportMode ? false : ((handset.IsArmed != NULL) ? handset.IsArmed() : false);
  bool skipSync = InBindingMode ||
    // TLM_RATIO_DISARMED keeps sending sync packets even when armed until the RX stops sending telemetry and the TLM=Off has taken effect
    (isTlmDisarmed && armed && (ExpressLRS_currTlmDenom == 1));

//   uint8_t NonceFHSSresult = OtaNonce % ExpressLRS_currAirRate_Modparams->FHSShopInterval;

    // DBGLN("skipSync = %u, now = %u, SyncPacketLastSent = %u, SyncInterval = %u", skipSync, now, SyncPacketLastSent, SyncInterval);
    // Sync spam only happens on slot 1 and 2 and can't be disabled
    if ((syncSpamCounter && (syncPacketCount % 10 == 0) /*|| (syncSpamCounterAfterRateChange && FHSSonSyncChannel())*/) /*&& (NonceFHSSresult == 1 || NonceFHSSresult == 2)*/)
    {
        // DBGLN("Sending sync packet");
        otaPkt.std.type = PACKET_TYPE_SYNC;
        GenerateSyncPacketData(OtaIsFullRes ? &otaPkt.full.sync.sync : &otaPkt.std.sync);
        OtaGeneratePacketCrc(&otaPkt);
        p = (uint8_t *)&otaPkt.full.sync;
        QueueTxPayload(p);
        if (!TxPayloadPending) syncSpamCounter++;
        // syncSlot = 0; // reset the sync slot in case the new rate (after the syncspam) has a lower FHSShopInterval
    }
    // Regular sync rotates through 4x slots, twice on each slot, and telemetry pushes it to the next slot up
    // But only on the sync FHSS channel and with a timed delay between them
    else if ((!skipSync) /*&& ((syncSlot / 2) <= NonceFHSSresult)*/ && (now - SyncPacketLastSent > SyncInterval) /*&& FHSSonSyncChannel()*/)
    {
        #if !SENSI_TEST
        // DBGLN("Sending sync packet periodically");
        otaPkt.std.type = PACKET_TYPE_SYNC;
        GenerateSyncPacketData(OtaIsFullRes ? &otaPkt.full.sync.sync : &otaPkt.std.sync);
        OtaGeneratePacketCrc(&otaPkt);
        p = (uint8_t *)&otaPkt.full.sync;
        // syncSlot = (syncSlot + 1) % (ExpressLRS_currAirRate_Modparams->FHSShopInterval * 2);
        QueueTxPayload(p);
        #endif
    }
    else
    {
#if ELRS_HAS_AIRPORT
        if (airportMode && !InBindingMode)
        {
            syncPacketCount++;
            OtaPackAirportData(&otaPkt, &AirportUartToRf);
            OtaGeneratePacketCrc(&otaPkt);
            QueueTxPayload((uint8_t *)&otaPkt.full.airport);
        }
        else
#endif
        if (MspSender.IsActive() && (NextPacketIsMspData || InBindingMode))
        {
            otaPkt.std.type = PACKET_TYPE_MSPDATA;
            if (OtaIsFullRes) {
                otaPkt.full.msp_ul.packageIndex = MspSender.GetCurrentPayload(otaPkt.full.msp_ul.payload, sizeof(otaPkt.full.msp_ul.payload));
            } else {
                otaPkt.std.msp_ul.packageIndex = MspSender.GetCurrentPayload(otaPkt.std.msp_ul.payload, sizeof(otaPkt.std.msp_ul.payload));
            }

            OtaGeneratePacketCrc(&otaPkt);
            p = (uint8_t *)&otaPkt.full.msp_ul;
            QueueTxPayload(p);
            if (TxPayloadPending)
                NextPacketIsMspData = false;
        }
        else
        {
        //   injectBackpackPanTiltRollData(now);

            if ((isRcData || TelemetryConfirmPending) && !InBindingMode) {
                syncPacketCount++;
                p = (uint8_t *)&otaPkt.full.rc;
                OtaPackChannelData(&otaPkt, ChannelData, TelemetryReceiver.GetCurrentConfirm(), ExpressLRS_currTlmDenom);
                OtaGeneratePacketCrc(&otaPkt);
                // DBGLN("ExpressLRS_currAirRate_Modparams->PayloadLength = %u", ExpressLRS_currAirRate_Modparams->PayloadLength);
                QueueTxPayload(p);
                if (TxPayloadPending) {
                    TelemetryConfirmPending = false;
                    NextPacketIsMspData = true;
                }
            }
        }
    }
}

#if !ELRS_AIRPORT
static void ModelUpdateReq(void)
{
  // Force synspam with the current rate parameters in case already have a connection established
  if (txConfig.SetModelId(txConfig.m_modelId))
  {
    syncSpamCounter = syncSpamAmount;
    syncSpamCounterAfterRateChange = syncSpamAmountAfterRateChange;
    ModelUpdatePending = true;
  }

  devicesTriggerEvent();

  // Jump from awaitingModelId to transmitting to break the startup delay now
  // that the ModelID has been confirmed by the handset
  if (connectionState == awaitingModelId)
  {
    connectionState = disconnected;
  }
}
#endif

static void setup(void)
{
    wdt_init_t    wdt_param           = {.mode = RESET_MODE, .count = 6000};

    if (setupHardwareFromOptions())
    {
        UnifiedConfig_Init(UNIFIED_ROLE_TX, firmware_menu_version, firmware_build_id,
                           UnifiedStopNormalOperation, UnifiedRequestReboot);
#if ELRS_UNIFIED
        UnifiedConfig_StartBootProbe();
        const uint32_t probeStarted = millis();
        while (!UnifiedConfig_IsSessionActive() && (millis() - probeStarted) < 90U) {
            UnifiedConfig_Update(millis());
        }
        UnifiedConfig_EndBootProbe();
        if (UnifiedConfig_IsSessionActive()) {
            connectionState = serialUpdate;
            if (LED_device.initialize != NULL) LED_device.initialize();
            if (LED_device.start != NULL) (void)LED_device.start();
            if (LED_device.event != NULL) (void)LED_device.event();
            wdt_init(&wdt_param);
            wdt_enable();
            return;
        }
#endif
        TxConfig_Init(&txConfig);
        ELRS_EEPROM_Init(&eeprom);

        txConfig.SetStorageProvider(&eeprom); // Pass pointer to the Config class for access to storage
        #ifndef SIM_TUBE
        txConfig.Load(); // Load the stored values from eeprom
        #endif

        // Apply power configuration BEFORE radio device init.
        // devRadioTx.initialize() uses POWERMGNT_getPowerIndBm() to configure Tk86xxInit().
        POWERMGNT_init();
        DynamicPower_Init();
        if (txConfig.GetDynamicPower()) {
            POWERMGNT_setPower(POWERMGNT_getMinPower());
        } else {
            POWERMGNT_setPower((PowerLevels_e)txConfig.GetPower());
        }

#if ELRS_HAS_AIRPORT
        if (UnifiedConfig_IsAirport()) {
          Tk86xxSerialConfig serialConfig = {
            .baudRate = AIRPORT_UART_BAUD,
            .wordLength = TK86XX_SERIAL_WORD_LENGTH_8B,
            .parity = TK86XX_SERIAL_PARITY_NONE,
            .stopBits = TK86XX_SERIAL_STOP_BITS_1,
            .duplex = TK86XX_SERIAL_DUPLEX_FULL,
        };
          Tk86xxSerialInit(&serialConfig);
          AirportFifo_Reset(&AirportUartToRf);
          AirportFifo_Reset(&AirportRfToUart);
          Tk86xxSerialRegisterRxCallback(AirportUartRxCallback);
        } else
#endif
#if !ELRS_AIRPORT
        {
#if defined(USE_SBUS_PROTOCOL) || defined(USE_INVERTED_SBUS_PROTOCOL)
          Tk86xxSerialConfig serialConfig = {
            .baudRate = 100000,
            .wordLength = TK86XX_SERIAL_WORD_LENGTH_8B,
            .parity = TK86XX_SERIAL_PARITY_EVEN,
            .stopBits = TK86XX_SERIAL_STOP_BITS_2,
            .duplex = TK86XX_SERIAL_DUPLEX_HALF,
        };
          Tk86xxSerialInit(&serialConfig);
#else
          Tk86xxSerialConfig serialConfig = {
            .baudRate = crsfSerialPortBaudEnumToBaud((crsf_serial_baud_e)txConfig.GetCrsfSerialBaudEnum()),
            .wordLength = TK86XX_SERIAL_WORD_LENGTH_8B,
            .parity = TK86XX_SERIAL_PARITY_NONE,
            .stopBits = TK86XX_SERIAL_STOP_BITS_1,
            .duplex = TK86XX_SERIAL_DUPLEX_HALF,
        };
          Tk86xxSerialInit(&serialConfig);
#endif
        }
#endif
        UnifiedConfig_SetLoggingEnabled(!UnifiedConfig_IsAirport());
        DBGLN("ELRS TX %s", firmware_build_id);
#if SENSI_TEST
#if SENSI_TEST_PROFILE
        DBGLN("SENSI TX module-pair");
#else
        DBGLN("SENSI TX signal-generator");
#endif
#endif

        StubbornReceiver_Init(&TelemetryReceiver);
        StubbornSender_Init(&MspSender);
#if !ELRS_AIRPORT
        if (!UnifiedConfig_IsAirport()) {
          devHandset_RegisterSendRCdataToRF(SendRCdataToRF);
        }
#endif
        setupBindingFromConfig();
        FHSSrandomiseFHSSsequence(uidMacSeedGet());
#if ELRS_HAS_AIRPORT
        if (UnifiedConfig_IsAirport()) {
          SetRFLinkRate(AIRPORT_RF_RATE);
          ExpressLRS_currTlmDenom = TLMratioEnumToValue(TLM_RATIO_1_2);
        } else
#endif
#if !ELRS_AIRPORT
        {
          ChangeRadioParams();
        }
#endif
        // Register the devices with the framework
#if ELRS_HAS_AIRPORT
        if (UnifiedConfig_IsAirport()) {
          devicesRegister(airport_ui_devices, ARRAY_SIZE(airport_ui_devices));
        } else
#endif
#if !ELRS_AIRPORT
        {
          devicesRegister(rc_ui_devices, ARRAY_SIZE(rc_ui_devices));
        }
#endif
        // Initialise the devices
        devicesInit();
        DBGLN("Initialised devices");
        GENERIC_CRC8Init(CRSF_CRC_POLY);
        eclic_priority_group_set(ECLIC_PRIGROUP_LEVEL3_PRIO0); // 10us
        wdt_init(&wdt_param); // 7us
        wdt_enable();

        DevRadioTx_RegisterRxDoneCb(RXdoneISR);
        DevRadioTx_RegisterTxDoneCb(TXdoneISR);
        DevRadioTx_RegisterTxAbortCb(ClearTxPayloadPending);
        DevRadioTx_RegisterTlmWindowDoneCb(DownlinkTlmWindowDone);

#if !ELRS_AIRPORT
        if (!UnifiedConfig_IsAirport() && handset.registerCallbacks)
        {
          handset.registerCallbacks(NULL, NULL, ModelUpdateReq, EnterBindingModeSafely);
        }
#endif

        DBGLN("ExpressLRS TX Module Booted...");

        // Radio.currFreq = FHSSgetInitialFreq(); //set frequency first or an error will occur!!!
        bool init_success = true;
        // #if defined(USE_BLE_JOYSTICK)
        // init_success = true; // No radio is attached with a joystick only module.  So we are going to fake success so that crsf, hwTimer etc are initiated below.
        // #else
        // if (GPIO_PIN_SCK != UNDEF_PIN)
        // {
        //     init_success = Radio.Begin(FHSSgetMinimumFreq(), FHSSgetMaximumFreq());
        // }
        // else
        // {
        //     // Assume BLE Joystick mode if no radio SCK pin
        //     init_success = true;
        // }
        // #endif

        if (!init_success)
        {
            connectionState = radioFailed;
        }
        else
        {
            TelemetryReceiver.SetDataToReceive(CRSFinBuffer, sizeof(CRSFinBuffer));

            // Set the pkt rate, TLM ratio, and power from the stored eeprom values
            // ChangeRadioParams();

        // #if defined(Regulatory_Domain_EU_CE_2400)
        //     SetClearChannelAssessmentTime();
        // #endif
        //     hwTimer::init(NULL, timerCallback);
        //     connectionState = noCrossfire;
        }
    } else {
        // In the failure case we set the logging to the null logger so nothing crashes
        // if it decides to log something
        // TxBackpack = new NullStream();
    }

    registerButtonFunction(ACTION_BIND, EnterBindingMode);
    // registerButtonFunction(ACTION_INCREASE_POWER, cyclePower);

    // connectionState = disconnected;
    devicesStart();
}

static void ExitBindingMode()
{
  if (!InBindingMode)
    return;

  MspSender.ResetState();

  // Reset CRCInit to UID-defined value
  OtaUpdateCrcInitFromUid();
  InBindingMode = false; // Clear binding mode before SetRFLinkRate() for correct IQ

#if ELRS_HAS_AIRPORT
  if (UnifiedConfig_IsAirport()) {
    SetRFLinkRate(AIRPORT_RF_RATE);
    ExpressLRS_currTlmDenom = TLMratioEnumToValue(TLM_RATIO_1_2);
  } else
#endif
#if !ELRS_AIRPORT
  {
    SetRFLinkRate(txConfig.GetRate()); //return to original rate
    ExpressLRS_currTlmDenom = TLMratioEnumToValue(ExpressLRS_currAirRate_Modparams->TLMinterval);
  }
#endif

  DBGLN("Exiting binding mode");
  devicesTriggerEvent();
}

static void ConfigChangeCommit()
{
  // Adjust the air rate based on teh current baud rate
  // auto index = adjustPacketRateForBaud(config.GetRate());
  // config.SetRate(index);

  // Write the uncommitted eeprom values (may block for a while)
  txConfig.Commit();
  // Change params after the blocking finishes as a rate change will change the radio freq
  ChangeRadioParams();
  // Clear the commitInProgress flag so normal processing resumes
  commitInProgress = false;
  // UpdateFolderNames is expensive so it is called directly instead of in event() which gets called a lot
  luadevUpdateFolderNames();
  devicesTriggerEvent();
}

static void CheckConfigChangePending()
{
  if (txConfig.IsModified() || ModelUpdatePending)
  {
    // DBGLN("tx modified :%d", syncSpamCounter);
    // Keep transmitting sync packets until the spam counter runs out
    if (syncSpamCounter > 0)
      return;

#if !defined(PLATFORM_STM32) || defined(TARGET_USE_EEPROM)
    // while (busyTransmitting); // wait until no longer transmitting
#else
    // The code expects to enter here shortly after the tock ISR has started sending the last
    // sync packet, before the tick ISR. Because the EEPROM write takes so long and disables
    // interrupts, FastForward the timer
    const uint32_t EEPROM_WRITE_DURATION = 30000; // us, a page write on F103C8 takes ~29.3ms
    const uint32_t cycleInterval = ExpressLRS_currAirRate_Modparams->interval;
    // Total time needs to be at least DURATION, rounded up to next cycle
    // adding one cycle that will be eaten by busywaiting for the transmit to end
    uint32_t pauseCycles = ((EEPROM_WRITE_DURATION + cycleInterval - 1) / cycleInterval) + 1;
    // Pause won't return until paused, and has just passed the tick ISR (but not fired)
    hwTimer::pause(pauseCycles * cycleInterval);

    while (busyTransmitting); // wait until no longer transmitting

    --pauseCycles; // the last cycle will actually be a transmit
    while (pauseCycles--)
      nonceAdvance();
#endif
    // Set the commitInProgress flag to prevent any other RF SPI traffic during the commit from RX or scheduled TX
    commitInProgress = true;
    // If telemetry expected in the next interval, the radio was in RX mode
    // and will skip sending the next packet when the timer resumes.
    // Return to normal send mode because if the skipped packet happened
    // to be on the last slot of the FHSS the skip will prevent FHSS
    // if (TelemetryRcvPhase != ttrpTransmitting)
    // {
    //   Radio.SetTxIdleMode();
    //   TelemetryRcvPhase = ttrpTransmitting;
    // }
    ConfigChangeCommit();
  }
}

#if !SENSI_TEST
static void UpdateConnectDisconnectStatus()
{
  // Number of telemetry packets which can be lost in a row before going to disconnected state
  const unsigned RX_LOSS_CNT = 5;
  // Must be at least 512ms and +2 to account for any rounding down and partial millis()
  const uint32_t msConnectionLostTimeout = MAX((uint32_t)1024U, (uint32_t)ExpressLRS_currTlmDenom * ExpressLRS_currAirRate_Modparams->interval / (1000U / RX_LOSS_CNT)) + 2U;
  // Capture the last before now so it will always be <= now
  const uint32_t lastTlmMillis = LastTLMpacketRecvMillis;
  const uint32_t now = millis();
  if (lastTlmMillis && ((now - lastTlmMillis) <= msConnectionLostTimeout))
  {
    if (connectionState != connected)
    {
      connectionState = connected;
      CRSFHandset.ForwardDevicePings = true;
      // DBGLN("got downlink conn");
      lastLostMillis = now + 10 * 1000;
      // uartInputBuffer.flush();
    }
  }
  // If past RX_LOSS_CNT, or in awaitingModelId state for longer than DisconnectTimeoutMs, go to disconnected
  else if (connectionState == connected /* || (now - rfModeLastChangedMS) > ExpressLRS_currAirRate_RFperfParams->DisconnectTimeoutMs */)
  {
    connectionState = disconnected;
    connectionHasModelMatch = true;
    CRSFHandset.ForwardDevicePings = false;
    // DBGLN("lost downlink conn");
  }
}

// #ifndef SIM_TUBE
static char reconnectDone = 0;
// #endif
#endif
static void loop(void)
{
  uint32_t now = millis();

  UnifiedConfig_Update(now);
  if (txRebootRequested) {
    return;
  }
  wdt_feed();
  if (UnifiedConfig_IsSessionActive()) {
    return;
  }

#if ELRS_HAS_AIRPORT
  if (UnifiedConfig_IsAirport()) {
    AirportFlushUartOutput();
    SendRCdataToRF(false);
  }
#endif

  if (RxOtaModeRequested)
  {
    RxOtaModeRequested = false;
    TxPayloadPending = false;
    if (RxOtaSender_Start())
    {
      RxOtaModeWasActive = true;
      UpdateLuaDuringRxOta();
      return;
    }
  }

  if (RxOtaSender_IsActive())
  {
    RxOtaSender_Update();
    RxOtaModeWasActive = true;
    UpdateLuaDuringRxOta();
    return;
  }

  if (RxOtaModeWasActive)
  {
    RxOtaModeWasActive = false;
    TxPayloadPending = false;
    txLostSignal = true;
    devicesTriggerEvent();
  }

//   HandleUARTout(); // Only used for non-CRSF output

//   #if defined(USE_BLE_JOYSTICK)
//   if (connectionState != bleJoystick && connectionState != noCrossfire) // Wait until the correct crsf baud has been found
//   {
//       connectionState = bleJoystick;
//   }
//   #endif

#if !SENSI_TEST
  if (connectionState < MODE_STATES)
  {
    UpdateConnectDisconnectStatus();
  }
#endif

//   // Update UI devices
  devicesUpdate(now);

//   // Not a device because it must be run on the loop core
//   checkBackpackUpdate();

//   #if defined(PLATFORM_ESP8266) || defined(PLATFORM_ESP32)
//     // If the reboot time is set and the current time is past the reboot time then reboot.
//     if (rebootTime != 0 && now > rebootTime) {
//       ESP.restart();
//     }
//   #endif

//   executeDeferredFunction(micros());

//   HandleUARTin();

//   if (connectionState > MODE_STATES)
//   {
//     return;
//   }

//   CheckReadyToSend();
  CheckConfigChangePending();
  if (txConfig.GetDynamicPower())
  {
    // If telemetry is missing, notify dynpower once per expected interval.
    static uint32_t lastDynpowerMissedNotifiedMs = 0;
    const bool armed = (handset.IsArmed != NULL) ? handset.IsArmed() : false;
    if (armed && (LastTLMpacketRecvMillis != 0))
    {
      uint32_t linkstatsInterval = (uint32_t)ExpressLRS_currTlmDenom * (uint32_t)ExpressLRS_currAirRate_Modparams->interval / 500U;
      if (linkstatsInterval < 512U) linkstatsInterval = 512U;
      if ((now - LastTLMpacketRecvMillis) > (linkstatsInterval + 2U) &&
          (lastDynpowerMissedNotifiedMs == 0 || (now - lastDynpowerMissedNotifiedMs) > linkstatsInterval))
      {
        DynamicPower_TelemetryUpdate(DYNPOWER_UPDATE_MISSED);
        lastDynpowerMissedNotifiedMs = now;
      }
    }
  }
  DynamicPower_Update(now);

  /* Send TLM updates to handset if connected + reporting period
   * is elapsed. This keeps handset happy dispite of the telemetry ratio */
  if ((connectionState == connected) && (LastTLMpacketRecvMillis != 0) &&
      (now >= (uint32_t)(firmwareOptions.tlm_report_interval + TLMpacketReported)))
  {
    uint8_t linkStatisticsFrame[CRSF_FRAME_NOT_COUNTED_BYTES + CRSF_FRAME_SIZE(sizeof(crsfLinkStatistics_t))];

    CRSFHandset_makeLinkStatisticsPacket(linkStatisticsFrame);
    if (handset.sendTelemetryToTX) handset.sendTelemetryToTX(linkStatisticsFrame);
    // DBGLN("send link statistics packet");
    // sendCRSFTelemetryToBackpack(linkStatisticsFrame);
    TLMpacketReported = now;
  }

#if !SENSI_TEST
  if ((connectionState == disconnected)) { 
    if (now >= (uint32_t)(lastLostMillis)) {
      // #ifndef SIM_TUBE
      // DBGLN("lost signal, restart Radio");
      if ((TLM_RATIO_NO_TLM == ExpressLRS_currTlmDenom) && !reconnectDone) {
        reconnectDone = 1;
        lastLostMillis = now + 5000;
        txLostSignal = true;
        devicesTriggerEvent();
      } else if ((TLM_RATIO_NO_TLM != ExpressLRS_currTlmDenom)) {
        lastLostMillis = now + 5000;
        txLostSignal = true;
        devicesTriggerEvent();
      }
      // #endif
    }
  }
#endif

  if (TelemetryReceiver.HasFinishedData())
  {
    // DBGLN("HasFinishedData");
    luadevHandleRxLuaTelemetry(CRSFinBuffer);
    // Send all other tlm to handset
    if (handset.sendTelemetryToTX)
        handset.sendTelemetryToTX(CRSFinBuffer);
    // sendCRSFTelemetryToBackpack(CRSFinBuffer);
    TelemetryReceiver.Unlock();
  }
	
	  // only send msp data when binding is not active
	  static bool mspTransferActive = false;
	  if (InBindingMode)
	  {
	    // exit bind mode if package after some repeats
	    if (BindingSendCount > BindingSpamAmount) {
	      ExitBindingMode();
	    }
	  }
	  else if (!MspSender.IsActive())
	  {
	    // sending is done and we need to update our flag
	    if (mspTransferActive)
	    {
	      // unlock buffer for msp messages
	      CRSF_UnlockMspMessage();
	      mspTransferActive = false;
	    }
	    // we are not sending so look for next msp package
	    else
	    {
	      uint8_t *mspData = NULL;
	      uint8_t mspLen = 0;
	      CRSF_GetMspMessage(&mspData, &mspLen);
	      // if we have a new msp package start sending
	      if (mspData != NULL && mspLen > 0)
	      {
	        MspSender.SetDataToTransmit(mspData, mspLen);
	        mspTransferActive = true;
	      }
	    }
	  }
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
