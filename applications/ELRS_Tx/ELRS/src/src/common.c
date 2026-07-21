#include <stddef.h>
#include "common.h"
#include "OTA.h"
#include "tk86xx_api.h"

#if !SENSI_TEST
expresslrs_mod_settings_t ExpressLRS_AirRateConfig[RATE_MAX] = {
    {0, RADIO_TYPE_TK8620_TMS, RATE_TMS_5HZ, TLM_RATIO_1_2, 200, 200000, OTA8_PACKET_SIZE, 1, RATE_MODE_5},      
    {1, RADIO_TYPE_TK8620_TMS, RATE_TMS_10HZ, TLM_RATIO_1_4, 100, 100000, OTA8_PACKET_SIZE, 1, RATE_MODE_6},   
    {2, RADIO_TYPE_TK8620_TMS, RATE_TMS_16_6_HZ, TLM_RATIO_1_8, 60, 60000, OTA8_PACKET_SIZE, 1, RATE_MODE_7},    
    {3, RADIO_TYPE_TK8620_TMS, RATE_TMS_25HZ, TLM_RATIO_1_8, 40, 40000, OTA8_PACKET_SIZE, 1, RATE_MODE_8},       
    {4, RADIO_TYPE_TK8620_TMS, RATE_TMS_50HZ, TLM_RATIO_1_16, 20, 20000, OTA8_PACKET_SIZE, 1, RATE_MODE_9},       
    {5, RADIO_TYPE_TK8620_TMS, RATE_TMS_100HZ, TLM_RATIO_1_32, 10, 10000, OTA8_PACKET_SIZE, 1, RATE_MODE_10},     
    {6, RADIO_TYPE_TK8620_TMS, RATE_TMS_200HZ, TLM_RATIO_1_64, 5, 5000, OTA8_PACKET_SIZE, 1, RATE_MODE_11},        
    {7, RADIO_TYPE_TK8620_TMS, RATE_TMS_250HZ, TLM_RATIO_1_64, 4, 4000, OTA8_PACKET_SIZE, 1, RATE_MODE_18},       
};
#else
expresslrs_mod_settings_t ExpressLRS_AirRateConfig[RATE_MAX] = {
    {0, RADIO_TYPE_TK8620_TMS, RATE_TMS_5HZ, TLM_RATIO_1_2, 200, 200000, OTA8_PACKET_SIZE, 1, RATE_MODE_5},      
    {1, RADIO_TYPE_TK8620_TMS, RATE_TMS_10HZ, TLM_RATIO_1_2, 100, 100000, OTA8_PACKET_SIZE, 1, RATE_MODE_6},   
    {2, RADIO_TYPE_TK8620_TMS, RATE_TMS_16_6_HZ, TLM_RATIO_1_2, 60, 60000, OTA8_PACKET_SIZE, 1, RATE_MODE_7},    
    {3, RADIO_TYPE_TK8620_TMS, RATE_TMS_25HZ, TLM_RATIO_1_64, 40, 40000, OTA8_PACKET_SIZE, 1, RATE_MODE_8},       
    {4, RADIO_TYPE_TK8620_TMS, RATE_TMS_50HZ, TLM_RATIO_1_64, 20, 20000, OTA8_PACKET_SIZE, 1, RATE_MODE_9},       
    {5, RADIO_TYPE_TK8620_TMS, RATE_TMS_100HZ, TLM_RATIO_1_64, 10, 10000, OTA8_PACKET_SIZE, 1, RATE_MODE_10},     
    {6, RADIO_TYPE_TK8620_TMS, RATE_TMS_200HZ, TLM_RATIO_1_64, 5, 5000, OTA8_PACKET_SIZE, 1, RATE_MODE_11},        
    {7, RADIO_TYPE_TK8620_TMS, RATE_TMS_250HZ, TLM_RATIO_1_64, 4, 4000, OTA8_PACKET_SIZE, 1, RATE_MODE_18},       
};
#endif

expresslrs_rf_pref_params_s ExpressLRS_AirRateRFperf[RATE_MAX] = {
    // DynpowerSnrThreshUp/Dn are set to DYNPOWER_SNR_THRESH_NONE to force RSSI-based dynamic power on TK8620.
    {0, -132,  200000, 7000, 2500, 600, 5000, DYNPOWER_SNR_THRESH_NONE, DYNPOWER_SNR_THRESH_NONE},
    {1, -129,  100000, 7000, 2500, 600, 5000, DYNPOWER_SNR_THRESH_NONE, DYNPOWER_SNR_THRESH_NONE},
    {2, -126,  60000, 7000, 2500, 600, 5000, DYNPOWER_SNR_THRESH_NONE, DYNPOWER_SNR_THRESH_NONE},
    {3, -123, 40000, 6000, 2500, 600, 5000, DYNPOWER_SNR_THRESH_NONE, DYNPOWER_SNR_THRESH_NONE},
    {4, -120, 20000, 5000, 4000, 600, 5000, DYNPOWER_SNR_THRESH_NONE, DYNPOWER_SNR_THRESH_NONE},
    {5, -117,  10000, 4000, 2500, 600, 5000, DYNPOWER_SNR_THRESH_NONE, DYNPOWER_SNR_THRESH_NONE},
    {6, -114,  5000, 3000, 2500, 600, 5000, DYNPOWER_SNR_THRESH_NONE, DYNPOWER_SNR_THRESH_NONE},
    {7, -112,  4000, 3000, 2500, 600, 5000, DYNPOWER_SNR_THRESH_NONE, DYNPOWER_SNR_THRESH_NONE},
};

expresslrs_mod_settings_t *get_elrs_airRateConfig(uint8_t index)
{
    if (RATE_MAX <= index)
    {
        // Set to last usable entry in the array
        index = RATE_MAX - 1;
    }
    return &ExpressLRS_AirRateConfig[index];
}

expresslrs_rf_pref_params_s *get_elrs_RFperfParams(uint8_t index)
{
    if (RATE_MAX <= index)
    {
        // Set to last usable entry in the array
        index = RATE_MAX - 1;
    }
    return &ExpressLRS_AirRateRFperf[index];
}

// uint8_t get_elrs_HandsetRate_max(uint8_t rateIndex, uint32_t minInterval)
// {
//     while (rateIndex < RATE_MAX)
//     {
//         expresslrs_mod_settings_t const * const ModParams = &ExpressLRS_AirRateConfig[rateIndex];
//         // Handset interval = time between packets from handset, which is expected to be air rate * number of times it is sent
//         uint32_t handsetInterval = ModParams->interval * ModParams->numOfSends;
//         if (handsetInterval >= minInterval)
//             break;
//         ++rateIndex;
//     }

//     return rateIndex;
// }

// uint8_t ICACHE_RAM_ATTR enumRatetoIndex(expresslrs_RFrates_e const eRate)
// { // convert enum_rate to index
//     expresslrs_mod_settings_t const * ModParams;
//     for (uint8_t i = 0; i < RATE_MAX; i++)
//     {
//         ModParams = get_elrs_airRateConfig(i);
//         if (ModParams->enum_rate == eRate)
//         {
//             return i;
//         }
//     }
//     // If 25Hz selected and not available, return the slowest rate available
//     // else return the fastest rate available (500Hz selected but not available)
//     return (eRate == RATE_LORA_25HZ) ? RATE_MAX - 1 : 0;
// }

// Connection state information
uint8_t UID[UID_LEN] = {0};  // "bind phrase" ID
bool connectionHasModelMatch = false;
bool teamraceHasModelMatch = true; // true if isTx or teamrace disabled or (enabled and channel in correct postion)
bool InBindingMode = false;
bool txLostSignal = false;
bool txPowerChanged = false;
bool tlmChanged = false;
connectionState_e elrsConnectionState = disconnected;
connectionState_e elrsConnectionStateBackup = disconnected;
void Tk86xxOnConnectionStateChanged(connectionState_e state)
{
    elrsConnectionState = state;
}
uint8_t ExpressLRS_currTlmDenom = 1;
expresslrs_mod_settings_t *ExpressLRS_currAirRate_Modparams = NULL;
expresslrs_rf_pref_params_s *ExpressLRS_currAirRate_RFperfParams = NULL;
uint8_t s_data_buf[BBU_TRX_MAX] = {0};

// Current state of channels, CRSF format
uint32_t ChannelData[CRSF_NUM_CHANNELS];

uint8_t TLMratioEnumToValue(expresslrs_tlm_ratio_e const enumval)
{
    // !! TLM_RATIO_STD/TLM_RATIO_DISARMED should be converted by the caller !!
    if (enumval == TLM_RATIO_NO_TLM)
        return 1;

    if (enumval == TLM_RATIO_STD) return 1 << (8 + TLM_RATIO_NO_TLM - ExpressLRS_currAirRate_Modparams->TLMinterval);
    // 1 << (8 - (enumval - TLM_RATIO_NO_TLM))
    // 1_128 = 128, 1_64 = 64, 1_32 = 32, etc
    return 1 << (8 + TLM_RATIO_NO_TLM - enumval);
}

// Maximum ms between LINK_STATISTICS packets for determining burst max
#define TELEM_MIN_LINK_INTERVAL_MS 512U
/***
 * @brief: Calculate number of 'burst' telemetry frames for the specified air rate and tlm ratio
 *
 * When attempting to send a LinkStats telemetry frame at most every TELEM_MIN_LINK_INTERVAL_MS,
 * calculate the number of sequential advanced telemetry frames before another LinkStats is due.
 ****/
uint8_t TLMBurstMaxForRateRatio(uint16_t const rateHz, uint8_t const ratioDiv)
{
    // telemInterval = 1000 / (hz / ratiodiv);
    // burst = TELEM_MIN_LINK_INTERVAL_MS / telemInterval;
    // This ^^^ rearranged to preserve precision vvv, using u32 because F1000 1:2 = 256
    unsigned retVal = TELEM_MIN_LINK_INTERVAL_MS * rateHz / ratioDiv / 1000U;

    // Reserve one slot for LINK telemetry. 256 becomes 255 here, safe for return in uint8_t
    if (retVal > 1)
        --retVal;
    else
        retVal = 1;
    //DBGLN("TLMburst: %d", retVal);

    return retVal;
}

uint32_t uidMacSeedGet()
{
    const uint32_t macSeed = ((uint32_t)UID[2] << 24) + ((uint32_t)UID[3] << 16) +
                             ((uint32_t)UID[4] << 8) + (UID[5]^OTA_VERSION_ID);
    return macSeed;
}

// bool ICACHE_RAM_ATTR isDualRadio()
// {
//     return GPIO_PIN_NSS_2 != UNDEF_PIN;
// }
