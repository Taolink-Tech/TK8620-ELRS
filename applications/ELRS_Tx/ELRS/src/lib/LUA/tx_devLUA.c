#include <stdbool.h>
#include <string.h>
#include "rxtx_devLua.h"
#include "CRSF.h"
#include "CRSFHandset.h"
#include "OTA.h"
#include "FHSS.h"
#include "helpers.h"
#include "config.h"
#include "tk86xx_platform.h"
#include "handset.h"
#include "logging.h"
#include "POWERMGNT.h"
#include "tx_power.h"
#include "serial_port.h"
#include "rx_ota_sender.h"

#define STR_LUA_ALLAUX         "AUX1;AUX2;AUX3;AUX4;AUX5;AUX6;AUX7;AUX8;AUX9;AUX10"

#define STR_LUA_ALLAUX_UPDOWN  "AUX1" LUASYM_ARROW_UP ";AUX1" LUASYM_ARROW_DN ";AUX2" LUASYM_ARROW_UP ";AUX2" LUASYM_ARROW_DN \
                               ";AUX3" LUASYM_ARROW_UP ";AUX3" LUASYM_ARROW_DN ";AUX4" LUASYM_ARROW_UP ";AUX4" LUASYM_ARROW_DN \
                               ";AUX5" LUASYM_ARROW_UP ";AUX5" LUASYM_ARROW_DN ";AUX6" LUASYM_ARROW_UP ";AUX6" LUASYM_ARROW_DN \
                               ";AUX7" LUASYM_ARROW_UP ";AUX7" LUASYM_ARROW_DN ";AUX8" LUASYM_ARROW_UP ";AUX8" LUASYM_ARROW_DN \
                               ";AUX9" LUASYM_ARROW_UP ";AUX9" LUASYM_ARROW_DN ";AUX10" LUASYM_ARROW_UP ";AUX10" LUASYM_ARROW_DN

#define STR_LUA_PACKETRATES "250Hz(-112dBm);200Hz(-114dBm);100Hz(-117dBm);50Hz(-120dBm);25Hz(-123dBm)"
#define LUA_PACKETRATE_COUNT 5U

#define RX_VERSION_FIELD_ID 2U

// extern char backpackVersion[];

static char version_domain[20+1+6+1];
static char rx_version[20+1+6+1] = "No RX";
static bool rx_version_refresh_pending;
char pwrFolderDynamicName[] = "TX Power (1000 Dynamic)";
char vtxFolderDynamicName[] = "VTX Admin (OFF:C:1 Aux11 )";
static char modelMatchUnit[] = " (ID: 00)";
static char tlmBandwidth[] = " (xxxxxbps)";
static char rxOtaStatusText[20];
// static const char folderNameSeparator[2] = {' ',':'};
static const char tlmRatios[] = "Std;1:128;1:64;1:32;1:16;1:8;1:4;1:2"; // "Off" and "Race" are intentionally hidden in this target UI.
static const char switchmodeOpts4ch[] = "Wide;Hybrid";
static const char switchmodeOpts8ch[] = "8ch;16ch Rate/2;12ch Mixed";
static const char crsfSerialBaudOpts[] = "400k;420k;921k";
// static const char antennamodeOpts[] = "Gemini;Ant 1;Ant 2;Switch";
// static const char antennamodeOptsDualBand[] = "Gemini;;;";
// static const char linkModeOpts[] = "Normal;MAVLink";
// static const char luastrDvrAux[] = "Off;" STR_LUA_ALLAUX_UPDOWN;
// static const char luastrDvrDelay[] = "0s;5s;15s;30s;45s;1min;2min";
// static const char luastrHeadTrackingEnable[] = "Off;On;" STR_LUA_ALLAUX_UPDOWN;
// static const char luastrHeadTrackingStart[] = STR_LUA_ALLAUX;
static const char luastrOffOn[] = "Off;On";
static char luastrPacketRates[] = STR_LUA_PACKETRATES;
// Product output power after the PA, displayed in mW.
static char txPowerLevels[] = "50;100;250;500;1000";
static const PowerLevels_e txPowerOptionMap[] = {
    TX_POWER_50mW,
    TX_POWER_100mW,
    TX_POWER_250mW,
    TX_POWER_500mW,
    TX_POWER_1000mW,
};
static int event();
#define HAS_RADIO true

static luaItem_selection_t luaAirRate = {
    {"Packet Rate", CRSF_TEXT_SELECTION},
    0, // value
    luastrPacketRates,
    STR_EMPTYSPACE
};

static luaItem_selection_t luaTlmRate = {
    {"Telem Ratio", CRSF_TEXT_SELECTION},
    0, // value
    tlmRatios,
    tlmBandwidth
};

static luaItem_selection_t luaModelMatch = {
    {"Model Match", CRSF_TEXT_SELECTION},
    0, // value
    luastrOffOn,
    modelMatchUnit
};

static luaItem_selection_t luaCrsfSerialBaud = {
    {"Serial Baud", CRSF_TEXT_SELECTION},
    0, // value
    crsfSerialBaudOpts,
    STR_EMPTYSPACE
};

//----------------------------POWER------------------
static luaItem_folder_t luaPowerFolder = {
    {"TX Power", CRSF_FOLDER}, pwrFolderDynamicName
};

static luaItem_selection_t luaPower = {
    {"Max Power", CRSF_TEXT_SELECTION},
    (uint8_t)TX_POWER_100mW, // default display: 100 mW product output / 20 dBm
    txPowerLevels,
    "mW"
};

static luaItem_selection_t luaDynamicPower = {
    {"Dynamic", CRSF_TEXT_SELECTION},
    0, // value
    "Off;Dyn;AUX9;AUX10;AUX11;AUX12",
    STR_EMPTYSPACE
};

#if defined(GPIO_PIN_FAN_EN) || defined(GPIO_PIN_FAN_PWM)
static luaItem_selection_t luaFanThreshold = {
    {"Fan Thresh", CRSF_TEXT_SELECTION},
    0, // value
    "10mW;25mW;50mW;100mW;250mW;500mW;1000mW;2000mW;Never",
    STR_EMPTYSPACE // units embedded so it won't display "NevermW"
};
#endif

#if defined(Regulatory_Domain_EU_CE_2400)
static struct luaItem_string luaCELimit = {
    {"100mW CE LIMIT", CRSF_INFO},
    STR_EMPTYSPACE
};
#endif

//----------------------------POWER------------------

static luaItem_selection_t luaSwitch = {
    {"Switch Mode", CRSF_TEXT_SELECTION},
    0, // value
    switchmodeOpts4ch,
    STR_EMPTYSPACE
};

#if defined(GPIO_PIN_NSS_2)
  static luaItem_selection_t luaAntenna = {
      {"Antenna Mode", CRSF_TEXT_SELECTION},
      0, // value
      antennamodeOpts,
      STR_EMPTYSPACE
  };
#endif

static struct luaItem_command luaBind = {
    {"Bind", CRSF_COMMAND},
    lcsIdle, // step
    STR_EMPTYSPACE
};

static struct luaItem_command luaRxOta = {
    {"RX OTA", CRSF_COMMAND},
    lcsIdle, // step
    STR_EMPTYSPACE
};

uint8_t luaCommandTimeout(const struct luaItem_command *cmd)
{
  if ((const void *)cmd == (const void *)&luaRxOta)
  {
    return 10;
  }
  return 200;
}

static struct luaItem_string luaInfo = {
    {"Bad/Good", (crsf_value_type_e)(CRSF_INFO | CRSF_FIELD_ELRS_HIDDEN)},
    STR_EMPTYSPACE
};

static struct luaItem_string luaELRSversion = {
    {"Version", CRSF_INFO},
    version_domain
};

static luaItem_folder_t luaOtherDevicesFolder = {
    {"Other Devices", CRSF_FOLDER}, NULL
};

static luaItem_folder_t luaRxDeviceFolder = {
    {"RX", CRSF_FOLDER}, NULL
};

static struct luaItem_string luaRxELRSversion = {
    {"Version", CRSF_INFO},
    rx_version
};

#if defined(PLATFORM_ESP32) || defined(PLATFORM_ESP8266)
static struct luaItem_command luaWebUpdate = {
    {"Enable WiFi", CRSF_COMMAND},
    lcsIdle, // step
    STR_EMPTYSPACE
};
#endif

#if defined(PLATFORM_ESP32)
static struct luaItem_command luaBLEJoystick = {
    {"BLE Joystick", CRSF_COMMAND},
    lcsIdle, // step
    STR_EMPTYSPACE
};
#endif

#if defined(GPIO_PIN_BACKPACK_EN)
static luaItem_selection_t luaBackpackEnable = {
    {"Backpack", CRSF_TEXT_SELECTION},
    0, // value
    luastrOffOn,
    STR_EMPTYSPACE};
#endif

static char luaBadGoodString[10];
static int event();

extern TxConfig_t txConfig;
extern void SetSyncSpam();
extern void StartRxOtaModeSafely(void) __attribute__((weak));
extern bool BackpackTelemReadyToSend;
#if defined(PLATFORM_ESP32) || defined(PLATFORM_ESP8266)
extern unsigned long rebootTime;
extern void setWifiUpdateMode();
#endif
/**
 * Lightweight itoa() replacement.
 *
 * Keeping this local avoids pulling in the larger printf/sprintf formatting
 * code for a simple integer conversion helper.
 *
 * - Supports bases 2..36 and falls back to base 10 outside that range.
 * - Emits a minus sign only for base-10 conversions.
 * - Returns the same buffer pointer passed in by the caller.
 */
static char *itoa(int value, char *str, int base)
{
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char *p = str;

    if (base < 2 || base > 36)
    {
        base = 10;
    }

    // Use unsigned math so INT_MIN converts safely without overflow.
    unsigned int u = (unsigned int)value;
    if (value < 0 && base == 10)
    {
        *p++ = '-';
        u = 0u - (unsigned int)value;
    }

    // Write the digits in reverse order, then flip the substring in place.
    char *start = p;
    do
    {
        unsigned int digit = u % (unsigned int)base;
        *p++ = digits[digit];
        u /= (unsigned int)base;
    } while (u != 0u);

    *p = '\0';

    // Reverse [start, p).
    for (char *l = start, *r = p - 1; l < r; ++l, --r)
    {
        char tmp = *l;
        *l = *r;
        *r = tmp;
    }

    return str;
}

static const uint8_t *luadevSkipString(const uint8_t *p, const uint8_t *end)
{
    while (p < end && *p != 0)
    {
        p++;
    }
    return (p < end) ? p + 1 : end;
}

static void luadevSetRxVersion(const char *value)
{
    if (value == NULL || *value == '\0')
    {
        return;
    }
    if (strcmp(rx_version, value) == 0)
    {
        return;
    }
    strlcpy(rx_version, value, sizeof(rx_version));
    setLuaStringValue(&luaRxELRSversion, rx_version);
    rx_version_refresh_pending = true;
}

static bool luadevHandleRxParameter(uint8_t *data)
{
    if (data == NULL || data[CRSF_TELEMETRY_LENGTH_INDEX] < CRSF_FRAME_LENGTH_EXT_TYPE_CRC)
    {
        return false;
    }

    const crsf_ext_header_t *header = (const crsf_ext_header_t *)data;
    if (header->orig_addr != CRSF_ADDRESS_CRSF_RECEIVER ||
        header->type != CRSF_FRAMETYPE_PARAMETER_SETTINGS_ENTRY)
    {
        return false;
    }

    const uint8_t *end = data + CRSF_FRAME_SIZE(data[CRSF_TELEMETRY_LENGTH_INDEX]) - 1U;
    const uint8_t *payload = &data[5];
    if ((end - payload) < 4)
    {
        return true;
    }

    const uint8_t fieldId = payload[0];
    const uint8_t chunksRemain = payload[1];
    const uint8_t *p = payload + 2; // field id + chunks remain
    if ((end - p) < 2 || (p[1] & CRSF_FIELD_TYPE_MASK) != CRSF_INFO)
    {
        return true;
    }

    p += 2; // parent + type
    const uint8_t *name = p;
    p = luadevSkipString(p, end);
    if (p < end && chunksRemain == 0U &&
        (fieldId == RX_VERSION_FIELD_ID ||
         strcmp((const char *)name, "Version") == 0 ||
         strcmp((const char *)name, "ELRS Version") == 0))
    {
        luadevSetRxVersion((const char *)p);
        return true;
    }
    return true;
}

void luadevHandleRxLuaTelemetry(uint8_t *data)
{
    (void)luadevHandleRxParameter(data);
}

static uint8_t luadevGetModelID()
{
  if (CRSFHandset.getModelID != NULL)
  {
    return CRSFHandset.getModelID();
  }
  return (uint8_t)txConfig.m_modelId;
}

static void luadevUpdateModelID()
{
  // In SBUS builds, CRSF handset init is compiled out, so function pointers can be NULL.
  strcpy(modelMatchUnit, " (ID: ");
  itoa(luadevGetModelID(), modelMatchUnit + strlen(modelMatchUnit), 10);
  strcat(modelMatchUnit, ")");
}

// static void luadevUpdateTlmBandwidth()
// {
// //   expresslrs_tlm_ratio_e eRatio = (expresslrs_tlm_ratio_e)txConfig.GetTlm();
// //   // TLM_RATIO_STD / TLM_RATIO_DISARMED
// //   if (eRatio == TLM_RATIO_STD || eRatio == TLM_RATIO_DISARMED)
// //   {
// //     // For Standard ratio, display the ratio instead of bps
// //     strcpy(tlmBandwidth, " (1:");
// //     uint8_t ratioDiv = TLMratioEnumToValue(ExpressLRS_currAirRate_Modparams->TLMinterval);
// //     itoa(ratioDiv, &tlmBandwidth[4], 10);
// //     strcat(tlmBandwidth, ")");
// //   }

// //   // TLM_RATIO_NO_TLM
// //   else if (eRatio == TLM_RATIO_NO_TLM)
// //   {
// //     tlmBandwidth[0] = '\0';
// //   }

// //   // All normal ratios
// //   else
// //   {
// //     tlmBandwidth[0] = ' ';

// //     uint16_t hz = 1000000 / ExpressLRS_currAirRate_Modparams->interval;
// //     uint8_t ratiodiv = TLMratioEnumToValue(eRatio);
// //     uint8_t burst = TLMBurstMaxForRateRatio(hz, ratiodiv);
// //     uint8_t bytesPerCall = OtaIsFullRes ? ELRS8_TELEMETRY_BYTES_PER_CALL : ELRS4_TELEMETRY_BYTES_PER_CALL;
// //     uint32_t bandwidthValue = bytesPerCall * 8U * burst * hz / ratiodiv / (burst + 1);
// //     if (OtaIsFullRes)
// //     {
// //       // Due to fullres also packing telemetry into the LinkStats packet, there is at least
// //       // N bytes more data for every rate except 100Hz 1:128, and 2*N bytes more for many
// //       // rates. The calculation is a more complex though, so just approximate some of the
// //       // extra bandwidth
// //       bandwidthValue += 8U * (ELRS8_TELEMETRY_BYTES_PER_CALL - sizeof(OTA_LinkStats_s));
// //     }

// //     itoa(bandwidthValue, &tlmBandwidth[2], 10);
// //     strcat(tlmBandwidth, "bps)");
// //   }
// }

// static void luadevUpdateBackpackOpts()
// {
//   if (txConfig.GetBackpackDisable())
//   {
//     // If backpack is disabled, set all the Backpack select options to "Disabled"
//     LUA_FIELD_HIDE(luaDvrAux);
//     LUA_FIELD_HIDE(luaDvrStartDelay);
//     LUA_FIELD_HIDE(luaDvrStopDelay);
//     LUA_FIELD_HIDE(luaHeadTrackingEnableChannel);
//     LUA_FIELD_HIDE(luaHeadTrackingStartChannel);
//     LUA_FIELD_HIDE(luaBackpackTelemetry);
//     // LUA_FIELD_HIDE(luaBackpackVersion);
//   }
//   else
//   {
//     LUA_FIELD_SHOW(luaDvrAux);
//     LUA_FIELD_SHOW(luaDvrStartDelay);
//     LUA_FIELD_SHOW(luaDvrStopDelay);
//     LUA_FIELD_SHOW(luaHeadTrackingEnableChannel);
//     LUA_FIELD_SHOW(luaHeadTrackingStartChannel);
//     LUA_FIELD_SHOW(luaBackpackTelemetry);
//     // LUA_FIELD_SHOW(luaBackpackVersion);
//   }
// }

static void luahandSimpleSendCmd(luaPropertiesCommon_t *item, uint8_t arg)
{
  const char *msg = "Sending...";
  static uint32_t lastLcsPoll;
  if ((void *)item == (void *)&luaRxOta &&
      (arg == lcsQuery || arg == lcsClick || arg == lcsExecuting ||
       arg == lcsConfirmed || arg == lcsCancel))
  {
    luaCmdStep_e step = lcsExecuting;
    rx_ota_sender_status_t status = RxOtaSender_GetStatus();

    if (arg == lcsConfirmed || arg == lcsCancel)
    {
      sendLuaCommandResponse((struct luaItem_command *)item, lcsIdle, STR_EMPTYSPACE);
      return;
    }

    if (arg != lcsQuery && !RxOtaSender_IsActive())
    {
      lastLcsPoll = millis();
      if (StartRxOtaModeSafely != NULL)
      {
        StartRxOtaModeSafely();
        status = RX_OTA_SENDER_HANDSHAKE;
      }
      else
      {
        status = RX_OTA_SENDER_FAILED;
      }
    }

    switch (status)
    {
    case RX_OTA_SENDER_HANDSHAKE:
      msg = "Handshake...";
      break;
    case RX_OTA_SENDER_UPGRADE:
      {
        uint8_t pct = RxOtaSender_GetProgressPercent();
        strcpy(rxOtaStatusText, "Upgrading ");
        itoa(pct, rxOtaStatusText + strlen(rxOtaStatusText), 10);
        strlcat(rxOtaStatusText, "%", sizeof(rxOtaStatusText));
        msg = rxOtaStatusText;
      }
      break;
    case RX_OTA_SENDER_DONE:
      msg = "Upgrade done";
      step = lcsAskConfirm;
      break;
    case RX_OTA_SENDER_VERSION_SAME:
      msg = "Version same";
      step = lcsAskConfirm;
      break;
    case RX_OTA_SENDER_FAILED:
      msg = "Upgrade failed";
      step = lcsAskConfirm;
      break;
    case RX_OTA_SENDER_IDLE:
    default:
      msg = (arg == lcsQuery) ? "Waiting..." : "Starting...";
      break;
    }

    sendLuaCommandResponse((struct luaItem_command *)item, step, msg);
  }
  else if (arg < lcsCancel)
  {
    lastLcsPoll = millis();
    if ((void *)item == (void *)&luaBind)
    {
      msg = "Binding...";
      EnterBindingModeSafely();
    }
    // else if ((void *)item == (void *)&luaVtxSend)
    // {
    // //   VtxTriggerSend();
    // }
    // else if ((void *)item == (void *)&luaRxWebUpdate)
    // {
    // //   RxWiFiReadyToSend = true;
    // }
    sendLuaCommandResponse((struct luaItem_command *)item, lcsExecuting, msg);
  } /* if doExecute */
  else if(arg == lcsCancel || ((millis() - lastLcsPoll)> 2000))
  {
    sendLuaCommandResponse((struct luaItem_command *)item, lcsIdle, STR_EMPTYSPACE);
  }
}

// static void updateFolderName_TxPower()
// {
// //   uint8_t txPwrDyn = txConfig.GetDynamicPower() ? txConfig.GetBoostChannel() + 1 : 0;
// //   uint8_t pwrFolderLabelOffset = 10; // start writing after "TX Power ("

// //   // Power Level
// //   pwrFolderLabelOffset += findLuaSelectionLabel(&luaPower, &pwrFolderDynamicName[pwrFolderLabelOffset], txConfig.GetPower() - MinPower);

// //   // Dynamic Power
// //   if (txPwrDyn)
// //   {
// //     pwrFolderDynamicName[pwrFolderLabelOffset++] = folderNameSeparator[0];
// //     pwrFolderLabelOffset += findLuaSelectionLabel(&luaDynamicPower, &pwrFolderDynamicName[pwrFolderLabelOffset], txPwrDyn);
// //   }

// //   pwrFolderDynamicName[pwrFolderLabelOffset++] = ')';
// //   pwrFolderDynamicName[pwrFolderLabelOffset] = '\0';
// }

// static void updateFolderName_VtxAdmin()
// {
// //   uint8_t vtxBand = txConfig.GetVtxBand();
// //   if (vtxBand)
// //   {
// //     luaVtxFolder.dyn_name = vtxFolderDynamicName;
// //     uint8_t vtxFolderLabelOffset = 11; // start writing after "VTX Admin ("

// //     // Band
// //     vtxFolderLabelOffset += findLuaSelectionLabel(&luaVtxBand, &vtxFolderDynamicName[vtxFolderLabelOffset], vtxBand);
// //     vtxFolderDynamicName[vtxFolderLabelOffset++] = folderNameSeparator[1];

// //     // Channel
// //     vtxFolderDynamicName[vtxFolderLabelOffset++] = '1' + txConfig.GetVtxChannel();

// //     // VTX Power
// //     uint8_t vtxPwr = config.GetVtxPower();
// //     //if power is no-change (-), don't show, also hide pitmode
// //     if (vtxPwr)
// //     {
// //       vtxFolderDynamicName[vtxFolderLabelOffset++] = folderNameSeparator[1];
// //       vtxFolderLabelOffset += findLuaSelectionLabel(&luaVtxPwr, &vtxFolderDynamicName[vtxFolderLabelOffset], vtxPwr);

// //       // Pit Mode
// //       uint8_t vtxPit = config.GetVtxPitmode();
// //       //if pitmode is off, don't show
// //       //show pitmode AuxSwitch or show P if not OFF
// //       if (vtxPit != 0)
// //       {
// //         if (vtxPit != 1)
// //         {
// //           vtxFolderDynamicName[vtxFolderLabelOffset++] = folderNameSeparator[1];
// //           vtxFolderLabelOffset += findLuaSelectionLabel(&luaVtxPit, &vtxFolderDynamicName[vtxFolderLabelOffset], vtxPit);
// //         }
// //         else
// //         {
// //           vtxFolderDynamicName[vtxFolderLabelOffset++] = folderNameSeparator[1];
// //           vtxFolderDynamicName[vtxFolderLabelOffset++] = 'P';
// //         }
// //       }
// //     }
// //     vtxFolderDynamicName[vtxFolderLabelOffset++] = ')';
// //     vtxFolderDynamicName[vtxFolderLabelOffset] = '\0';
// //   }
// //   else
// //   {
// //     //don't show vtx settings if band is OFF
// //     luaVtxFolder.dyn_name = NULL;
// //   }
// }

/***
 * @brief: Update the luaBadGoodString with the current bad/good count
 * This item is hidden on our Lua and only displayed in other systems that don't poll our status
 * Called from luaRegisterDevicePingCallback
 ****/
static void luadevUpdateBadGood()
{
  itoa(CRSFHandset.BadPktsCountResult, luaBadGoodString, 10);
  strcat(luaBadGoodString, "/");
  itoa(CRSFHandset.GoodPktsCountResult, luaBadGoodString + strlen(luaBadGoodString), 10);
}

/***
 * @brief: Update the dynamic strings used for folder names and labels
 ***/
void luadevUpdateFolderNames()
{
//   updateFolderName_TxPower();
//   updateFolderName_VtxAdmin();

  // These aren't folder names, just string labels slapped in the units field generally
//   luadevUpdateTlmBandwidth();
//   luadevUpdateBackpackOpts();
}

// static void recalculatePacketRateOptions(int minInterval)
// {
//     const char *allRates = STR_LUA_PACKETRATES;
//     const char *pos = allRates;
//     luastrPacketRates[0] = 0;
//     for (int i=0 ; i < RATE_MAX ; i++)
//     {
//         uint8_t rate = i;
//         rate = RATE_MAX - 1 - rate;
//         bool rateAllowed = (get_elrs_airRateConfig(rate)->interval * get_elrs_airRateConfig(rate)->numOfSends) >= minInterval;

//         const char *semi = strchrnul(pos, ';');
//         if (rateAllowed)
//         {
//             strncat(luastrPacketRates, pos, semi - pos);
//         }
//         pos = semi;
//         if (*semi == ';')
//         {
//             strcat(luastrPacketRates, ";");
//             pos = semi+1;
//         }
//     }

//     // trim off trailing semicolons (assumes luastrPacketRates has at least 1 non-semicolon)
//     for (uint8_t lastPos = strlen(luastrPacketRates)-1; luastrPacketRates[lastPos] == ';'; lastPos--)
//     {
//         luastrPacketRates[lastPos] = '\0';
//     }
// }

uint8_t adjustSwitchModeForAirRate(OtaSwitchMode_e eSwitchMode, uint8_t packetSize)
{
  // Only the fullres modes have 3 switch modes, so reset the switch mode if outside the
  // range for 4ch mode
  if (packetSize == OTA4_PACKET_SIZE)
  {
    if (eSwitchMode > smHybridOr16ch)
      return smWideOr8ch;
  }

  return eSwitchMode;
}

static void luaparamSetAirRate(luaPropertiesCommon_t *item, uint8_t arg)
{
    DBGLN("luaparamSetAirRate: %u", arg);
    if (arg < LUA_PACKETRATE_COUNT)
    {
      uint8_t selectedRate = RATE_TMS_250HZ - arg;
    //   uint8_t actualRate = adjustPacketRateForBaud(selectedRate);
    //     uint8_t actualRate = selectedRate;
    //   uint8_t newSwitchMode = adjustSwitchModeForAirRate(
    //     (OtaSwitchMode_e)txConfig.GetSwitchMode(), get_elrs_airRateConfig(actualRate)->PayloadLength);
      // Force Gemini when using dual band modes.
    //   uint8_t newAntennaMode = get_elrs_airRateConfig(actualRate)->radio_type == RADIO_TYPE_LR1121_LORA_DUAL ? TX_RADIO_MODE_GEMINI : txConfig.GetAntennaMode();
      // If the switch mode is going to change, block the change while connected
    //   bool isDisconnected = connectionState == disconnected;
      // Don't allow the switch mode to change if the TX is in mavlink mode
      // Wide switchmode is not compatible with mavlink, and the switchmode is
      // auto configuredwhen entering mavlink mode
      if (1) { // (newSwitchMode == OtaSwitchModeCurrent || (isDisconnected && !isMavlinkMode)) {
        txConfig.SetRate(selectedRate);
        // txConfig.SetSwitchMode(newSwitchMode);
        // txConfig.SetAntennaMode(newAntennaMode);
        // if (actualRate != selectedRate)
        // {
        //   setLuaWarningFlag(LUA_FLAG_ERROR_BAUDRATE, true);
        // }
        event();
      } else {
        setLuaWarningFlag(LUA_FLAG_ERROR_CONNECTED, true);
      }
    }
}

static void luaparamSetTlmRate(luaPropertiesCommon_t *item, uint8_t arg)
{
      expresslrs_tlm_ratio_e eRatio = (expresslrs_tlm_ratio_e)arg; 
      if (eRatio > 0) eRatio += 1; // Skip the hidden "Off" entry after "Std".
      // DBGLN("luaparamSetTlmRate: %u", eRatio);
      if (eRatio <= TLM_RATIO_DISARMED)
      {
        // Don't allow TLM ratio changes if using AIRPORT or Mavlink
        txConfig.SetTlm(eRatio);
        event();
      }
}

static void luaparamSetCrsfSerialBaud(luaPropertiesCommon_t *item, uint8_t arg)
{
    // Stored as enum (0..4). Apply on next reboot (handled in tx_main.c setup()).
    // DBGLN("luaparamSetCrsfSerialBaud: %u", arg);
    if (arg < CRSF_SERIAL_BAUD_MAX) {
        txConfig.SetCrsfSerialBaudEnum(arg);
    } else {
        txConfig.SetCrsfSerialBaudEnum(CRSF_SERIAL_BAUD_400K);
    }
    event();
}

static void luaparamSetSwitch(luaPropertiesCommon_t *item, uint8_t arg)
{
    // Only allow changing switch mode when disconnected since we need to guarantee
    // the pack and unpack functions are matched
    // Don't allow the switch mode to change if the TX is in mavlink mode
    // Wide switchmode is not compatible with mavlink, and the switchmode is
    // auto configuredwhen entering mavlink mode
    txConfig.SetSwitchMode(arg);
    OtaUpdateSerializers((OtaSwitchMode_e)arg, ExpressLRS_currAirRate_Modparams->PayloadLength);
    event();
}

// static void luaparamSetLinkMode(luaPropertiesCommon_t *item, uint8_t arg)
// {
//     // Only allow changing when disconnected since we need to guarantee
//     // the switch pack and unpack functions are matched on the tx and rx.
//     bool isDisconnected = connectionState == disconnected;
//     if (isDisconnected)
//     {
//     // txConfig.SetLinkMode(arg);
//     }
//     else
//     {
//     setLuaWarningFlag(LUA_FLAG_ERROR_CONNECTED, true);
//     }
// }

static void luaparamSetModelMatch(luaPropertiesCommon_t *item, uint8_t arg)
{
    UNUSED(item);
    const bool newModelMatch = arg != 0;
    txConfig.SetModelMatch(newModelMatch);
    setLuaTextSelectionValue(&luaModelMatch, newModelMatch ? 1U : 0U);

    if (connectionState == connected)
    {
        const uint8_t rxModelId = newModelMatch ? luadevGetModelID() : 0xff;
        mspPacket_t msp;
        MSP_packet_reset(&msp);
        MSP_packet_makeCommand(&msp);
        msp.function = MSP_SET_RX_CONFIG;
        MSP_packet_addByte(&msp, MSP_ELRS_MODEL_ID);
        MSP_packet_addByte(&msp, rxModelId);
        CRSF_AddMspMessage_packet(&msp, CRSF_ADDRESS_CRSF_RECEIVER);
    }

    luadevUpdateModelID();
}

static void ResetPower()
{
  DBGLN("rstPwr");
  // Dynamic Power starts at MinPower unless armed
  // (user may be turning up the power while flying and dropping the power may compromise the link)
  if (txConfig.GetDynamicPower())
  {
    const bool armed = (handset.IsArmed != NULL) ? handset.IsArmed() : false;
    if (!armed)
    {
      // if dynamic power enabled and not armed then set to MinPower
      POWERMGNT_setPower(POWERMGNT_getMinPower());
    }
    else if (POWERMGNT_currPower() < txConfig.GetPower())
    {
      // if the new config is a higher power then set it, otherwise leave it alone
      POWERMGNT_setPower((PowerLevels_e)txConfig.GetPower());
    }
  }
  else
  {
    POWERMGNT_setPower((PowerLevels_e)txConfig.GetPower());
  }
}

static void luaparamSetPower(luaPropertiesCommon_t *item, uint8_t arg)
{
    UNUSED(item);
    if (arg >= ARRAY_SIZE(txPowerOptionMap))
    {
        arg = 0;
    }
    uint8_t newPower = (uint8_t)txPowerOptionMap[arg];
    txConfig.SetPower(newPower);
    if (txConfig.IsModified())
    {
        ResetPower();
    }
}

static void luaparamSetDynamicPower(luaPropertiesCommon_t *item, uint8_t arg)
{
    UNUSED(item);
    DBGLN("setDynPwr: %u", arg);
    txConfig.SetDynamicPower(arg > 0);
    txConfig.SetBoostChannel((arg > 1) ? (arg - 1) : 0);
}

// static void luaparamSetFanThreshold(luaPropertiesCommon_t *item, uint8_t arg)
// {
//     txConfig.SetPowerFanThreshold(arg);
// }

// static void luaparamSetVtxBand(luaPropertiesCommon_t *item, uint8_t arg)
// {
//     // txConfig.SetVtxBand(arg);
// }

// static void luaparamSetVtxChannel(luaPropertiesCommon_t *item, uint8_t arg)
// {
//     // txConfig.SetVtxChannel(arg - 1);
// }

static void registerLuaParameters()
{
  if (HAS_RADIO) {
    registerLUAParameter(&luaAirRate, luaparamSetAirRate, 0);
    registerLUAParameter(&luaTlmRate, luaparamSetTlmRate, 0);
    registerLUAParameter(&luaCrsfSerialBaud, luaparamSetCrsfSerialBaud, 0);
    registerLUAParameter(&luaSwitch, luaparamSetSwitch, 0);

    // registerLUAParameter(&luaLinkMode, luaparamSetLinkMode, 0);
    registerLUAParameter(&luaModelMatch, luaparamSetModelMatch, 0);

    // POWER folder
    registerLUAParameter(&luaPowerFolder, NULL, 0);
    luaPower.options = txPowerLevels;
    registerLUAParameter(&luaPower, luaparamSetPower, luaPowerFolder.common.id);
    registerLUAParameter(&luaDynamicPower, luaparamSetDynamicPower, luaPowerFolder.common.id);
  }
//   if (GPIO_PIN_FAN_EN != UNDEF_PIN || GPIO_PIN_FAN_PWM != UNDEF_PIN) {
//     registerLUAParameter(&luaFanThreshold, luaparamSetFanThreshold, luaPowerFolder.common.id);
//   }
#if defined(Regulatory_Domain_EU_CE_2400)
  if (HAS_RADIO) {
    registerLUAParameter(&luaCELimit, NULL, luaPowerFolder.common.id);
  }
#endif
  if ((HAS_RADIO)) {
    // VTX folder
    // registerLUAParameter(&luaVtxFolder, NULL, 0);
    // registerLUAParameter(&luaVtxBand, luaparamSetVtxBand, luaVtxFolder.common.id);
    // registerLUAParameter(&luaVtxChannel, luaparamSetVtxChannel, luaVtxFolder.common.id);
    // registerLUAParameter(&luaVtxPwr, [](luaPropertiesCommon_t *item, uint8_t arg) {
    //   txConfig.SetVtxPower(arg);
    // }, luaVtxFolder.common.id);
    // registerLUAParameter(&luaVtxPit, [](luaPropertiesCommon_t *item, uint8_t arg) {
    //   txConfig.SetVtxPitmode(arg);
    // }, luaVtxFolder.common.id);
    // registerLUAParameter(&luaVtxSend, &luahandSimpleSendCmd, luaVtxFolder.common.id);
  }

  // WIFI folder
//   #if defined(PLATFORM_ESP32) || defined(PLATFORM_ESP8266)
//   registerLUAParameter(&luaWiFiFolder);
//   registerLUAParameter(&luaWebUpdate, &luahandWifiBle, luaWiFiFolder.common.id);
//   #else
//   if (HAS_RADIO) {
//     registerLUAParameter(&luaWiFiFolder);
//   }
//   #endif
  if (HAS_RADIO) {
    // registerLUAParameter(&luaRxWebUpdate, &luahandSimpleSendCmd, luaWiFiFolder.common.id);
  }

  #if defined(PLATFORM_ESP32)
  registerLUAParameter(&luaBLEJoystick, &luahandWifiBle);
  #endif

  if (HAS_RADIO) {
    registerLUAParameter(&luaBind, luahandSimpleSendCmd, 0);
    registerLUAParameter(&luaRxOta, luahandSimpleSendCmd, 0);
  }

  registerLUAParameter(&luaInfo, NULL, 0);
  if (strlen(firmware_menu_version) < 21) {
    strlcpy(version_domain, firmware_menu_version, 21);
    strlcat(version_domain, " ", sizeof(version_domain));
  } else {
    strlcpy(version_domain, firmware_menu_version, 18);
    strlcat(version_domain, "... ", sizeof(version_domain));
  }
  strlcat(version_domain, FHSSconfig->domain, sizeof(version_domain));
  registerLUAParameter(&luaELRSversion, NULL, 0);
  registerLUAParameter(&luaOtherDevicesFolder, NULL, 0);
  registerLUAParameter(&luaRxDeviceFolder, NULL, luaOtherDevicesFolder.common.id);
  registerLUAParameter(&luaRxELRSversion, NULL, luaRxDeviceFolder.common.id);
}

static int event()
{
  if (connectionState > FAILURE_STATES)
  {
    return DURATION_NEVER;
  }

//   uint8_t currentRate = adjustPacketRateForBaud(txConfig.GetRate());
    uint8_t currentRate = txConfig.GetRate();
//   recalculatePacketRateOptions(handset->getMinPacketInterval());
  if (currentRate < RATE_TMS_25HZ || currentRate >= RATE_MAX)
  {
    currentRate = RATE_TMS_250HZ;
  }
  setLuaTextSelectionValue(&luaAirRate, RATE_TMS_250HZ - currentRate);

  uint8_t tlmRatio = txConfig.GetTlm();
  // DBGLN("GET tlmRatio: %u", tlmRatio);
  if (tlmRatio != TLM_RATIO_STD) tlmRatio -= 1; // Skip the hidden "Off" entry after "Std".
  setLuaTextSelectionValue(&luaTlmRate, tlmRatio); 
  luaTlmRate.options = tlmRatios;

  setLuaTextSelectionValue(&luaCrsfSerialBaud, txConfig.GetCrsfSerialBaudEnum());

//   luaAntenna.options = get_elrs_airRateConfig(txConfig.GetRate())->radio_type == RADIO_TYPE_LR1121_LORA_DUAL ? antennamodeOptsDualBand : antennamodeOpts;

  setLuaTextSelectionValue(&luaSwitch, txConfig.GetSwitchMode());
  luaSwitch.options = OtaIsFullRes ? switchmodeOpts8ch : switchmodeOpts4ch;
  // setLuaTextSelectionValue(&luaLinkMode, txConfig.GetLinkMode());
  luadevUpdateModelID();
  setLuaTextSelectionValue(&luaModelMatch, (uint8_t)txConfig.GetModelMatch());
  uint8_t powerOption = 0;
  for (uint8_t i = 0; i < ARRAY_SIZE(txPowerOptionMap); ++i)
  {
    if (txConfig.GetPower() == (uint8_t)txPowerOptionMap[i])
    {
      powerOption = i;
      break;
    }
  }
  setLuaTextSelectionValue(&luaPower, powerOption);
//   if (GPIO_PIN_FAN_EN != UNDEF_PIN || GPIO_PIN_FAN_PWM != UNDEF_PIN)
//   {
//     setLuaTextSelectionValue(&luaFanThreshold, txConfig.GetPowerFanThreshold());
//   }

  uint8_t dynamic = txConfig.GetDynamicPower() ? (txConfig.GetBoostChannel() + 1) : 0;
  setLuaTextSelectionValue(&luaDynamicPower, dynamic);

//   setLuaTextSelectionValue(&luaVtxBand, txConfig.GetVtxBand());
//   setLuaUint8Value(&luaVtxChannel, txConfig.GetVtxChannel() + 1);
//   setLuaTextSelectionValue(&luaVtxPwr, txConfig.GetVtxPower());
  // Pit mode can only be sent as part of the power byte
//   LUA_FIELD_VISIBLE(luaVtxPit, txConfig.GetVtxPower() != 0);
//   setLuaTextSelectionValue(&luaVtxPit, txConfig.GetVtxPitmode());
  luadevUpdateFolderNames();
  return DURATION_IMMEDIATELY;
}

static int timeout()
{
  bool handledParameter = luaHandleUpdateParameter();
  if (!handledParameter && rx_version_refresh_pending)
  {
    rx_version_refresh_pending = false;
    luaParamUpdateReq(CRSF_FRAMETYPE_PARAMETER_READ, luaRxELRSversion.common.id, 0);
    handledParameter = luaHandleUpdateParameter();
  }
  if (handledParameter)
  {
    SetSyncSpam();
  }
  return DURATION_IMMEDIATELY;
}

static int start()
{
  if (connectionState > FAILURE_STATES)
  {
    return DURATION_NEVER;
  }
  handset.registerParameterUpdateCallback(luaParamUpdateReq);
  luaResetParameters();
  registerLuaParameters();

  setLuaStringValue(&luaInfo, luaBadGoodString);
  luaRegisterDevicePingCallback(&luadevUpdateBadGood);

  event();
  return DURATION_IMMEDIATELY;
}

device_t LUA_TxDevice = {
  .initialize = NULL,
  .start = start,
  .event = event,
  .timeout = timeout
};
