#include <string.h>
#include "config.h"
#include "config_legacy.h"
#include "common.h"
#include "POWERMGNT.h"
#include "OTA.h"
#include "helpers.h"
#include "logging.h"
#include "options.h"
#include "tk86xx_platform.h"
#include "flash_hal.h"

#define BIT(a)               (1 << (a))
#define MODEL_CHANGED       BIT(1)
#define VTX_CHANGED         BIT(2)
#define MAIN_CHANGED        BIT(3) // catch-all for global config item
#define FAN_CHANGED         BIT(4)
#define MOTION_CHANGED      BIT(5)
#define BUTTON_CHANGED      BIT(6)
#define ALL_CHANGED         (MODEL_CHANGED | VTX_CHANGED | MAIN_CHANGED | FAN_CHANGED | MOTION_CHANGED | BUTTON_CHANGED)

static void RxConfigCheckUpdateFlashedUid(bool skipDescrimCheck)
{
    (void)skipDescrimCheck;
}

static void RxConfigLoad(void)
{
    rxConfig.m_modified = false;
    rxConfig.m_eeprom->Get(0, (uint8_t*)&rxConfig.m_config, sizeof(rxConfig.m_config));

    uint32_t version = 0;
    if ((rxConfig.m_config.version & CONFIG_MAGIC_MASK) == RX_CONFIG_MAGIC) version = rxConfig.m_config.version & ~CONFIG_MAGIC_MASK;

    // If version is current, all done
    if (version == RX_CONFIG_VERSION)
    {
        RxConfigCheckUpdateFlashedUid(false);
        return;
    }

    // Can't upgrade from version <4, or when flashing a previous version, just use defaults.
    if (version < 4 || version > RX_CONFIG_VERSION)
    {
        rxConfig.SetDefaults(true);
        RxConfigCheckUpdateFlashedUid(true);
        return;
    }

}

static rx_config_bindstorage_t RxConfigGetBindStorage(void)
{
    return (rx_config_bindstorage_t)rxConfig.m_config.bindStorage;
}

static eSerialProtocol_e RxConfigGetSerialProtocol(void) 
{ 
    return (eSerialProtocol_e)rxConfig.m_config.serialProtocol; 
}

static enum eFailsafeMode RxConfigGetFailsafeMode(void)
{
    return (enum eFailsafeMode)rxConfig.m_config.failsafeMode;
}

static bool RxConfigGetIsBound(void)
{
    #ifdef SIM_TUBE
    return true;
    #else
    if (rxConfig.m_config.bindStorage == BINDSTORAGE_VOLATILE)
        return false;
    return UID_IS_BOUND(rxConfig.m_config.uid);
    #endif
}

static bool RxConfigIsOnLoan(void) 
{
    if (rxConfig.m_config.bindStorage != BINDSTORAGE_RETURNABLE)
        return false;
    if (!firmwareOptions.hasUID)
        return false;
    return rxConfig.GetIsBound() && memcmp(rxConfig.m_config.uid, firmwareOptions.uid, UID_LEN) != 0;
}

static bool erase_power_on_count = false;
static int realPowerOnCounter = -1;
static const uint8_t RxConfig_GetPowerOnCounter(void)
{
    #ifdef SIM_TUBE
    return 0;
    #else
    if (realPowerOnCounter == -1) {
        uint8_t zeros[16];
        rxConfig.m_eeprom->Get(POWERON_CNT_OFFSET, zeros, sizeof(zeros));
        realPowerOnCounter = sizeof(zeros);
        for (int i=0 ; i<sizeof(zeros) ; i++) {
            if (zeros[i] != 0) {
                realPowerOnCounter = i;
                break;
            }
        }
    }
    return realPowerOnCounter;
    #endif
}

static void RxConfigCommit(void)
{
    #ifdef SIM_TUBE
    return;
    #endif

    if (erase_power_on_count)
    {
        flash_user_erase(POWERON_CNT_OFFSET, sizeof(uint8_t));
        erase_power_on_count = false;
    }

    if (!rxConfig.m_modified)
    {
        // No changes
        return;
    }

    // Write the struct to eeprom
    rxConfig.m_eeprom->Put(0, &rxConfig.m_config);
    rxConfig.m_eeprom->Commit(0, (uint8_t*)&rxConfig.m_config, sizeof(rxConfig.m_config));

    rxConfig.m_modified = false;
}

void RxConfigSetUID(uint8_t* uid)
{
    for (uint8_t i = 0; i < UID_LEN; ++i)
    {
        rxConfig.m_config.uid[i] = uid[i];
    }
    rxConfig.m_modified = true;
}

#ifdef SIM_TUBE
static uint8_t uid[UID_LEN] = {0,0,0,0,0,1};
#endif
static const uint8_t* RxConfigGetUID(void) 
{ 
    #ifdef SIM_TUBE
    return uid;
    #else
    return rxConfig.m_config.uid; 
    #endif
}

static uint8_t  RxConfigGetModelId(void) 
{ 
    #ifdef SIM_TUBE
    return 0xff;
    #else
    return rxConfig.m_config.modelId; 
    #endif
}

static uint8_t RxConfigGetPower(void)
{
    #ifdef SIM_TUBE
    return (uint8_t)POWERMGNT_getDefaultPower();
    #else
    return rxConfig.m_config.power;
    #endif
}

void RxConfig_SetPowerOnCounter(uint8_t powerOnCounter)
{
    realPowerOnCounter = powerOnCounter;
    if (powerOnCounter == 0)
    {
        erase_power_on_count = true;
        rxConfig.m_modified = true;
    }
    else
    {
        uint8_t zeros[16] = {0};
        rxConfig.m_eeprom->Commit(POWERON_CNT_OFFSET, zeros, MIN((size_t)powerOnCounter, sizeof(zeros)));
    }
}

void RxConfig_SetModelId(uint8_t modelId)
{
    if (rxConfig.m_config.modelId != modelId)
    {
        rxConfig.m_config.modelId = modelId;
        rxConfig.m_modified = true;
    }
}

void RxConfig_SetPower(uint8_t power)
{
    #ifdef SIM_TUBE
    (void)power;
    #else
    // Allow PWR_MATCH_TX, otherwise clamp to device max.
    if (power != (uint8_t)PWR_MATCH_TX)
    {
        PowerLevels_e maxP = POWERMGNT_getMaxPower();
        if (power > (uint8_t)maxP) power = (uint8_t)maxP;
    }
    if (rxConfig.m_config.power != power)
    {
        rxConfig.m_config.power = power;
        rxConfig.m_modified = true;
    }
    #endif
}


static void RxConfigSetDefaults(bool commit)
{
    // Reset everything to 0/false and then just set anything that zero is not appropriate
    memset(&rxConfig.m_config, 0, sizeof(rxConfig.m_config));

    rxConfig.m_config.version = RX_CONFIG_VERSION | RX_CONFIG_MAGIC;
    rxConfig.m_config.modelId = 0xff;
    rxConfig.m_config.rateInitialIdx = RATE_TMS_250HZ;
    rxConfig.m_config.power = (uint8_t)POWERMGNT_getDefaultPower();

    rxConfig.m_config.teamraceChannel = AUX7; // CH11

    if (commit)
    {
        rxConfig.m_eeprom->Commit(0, (uint8_t*)&rxConfig.m_config, sizeof(rxConfig.m_config));
    }
}

static void RxConfigSetStorageProvider(ELRS_EEPROM_t *eeprom)
{
    if (eeprom) rxConfig.m_eeprom = eeprom;
}

void RxConfig_SetRateInitialIdx(uint8_t rateInitialIdx)
{
    if (rxConfig.m_config.rateInitialIdx != rateInitialIdx)
    {
        rxConfig.m_config.rateInitialIdx = rateInitialIdx;
        rxConfig.m_modified = true;
    }
}

static void RxConfigReturnLoan(void)
{
    if (rxConfig.IsOnLoan())
    {
        // go back to flashed UID if there is one
        // or unbind if there is not
        if (firmwareOptions.hasUID)
            memcpy(rxConfig.m_config.uid, firmwareOptions.uid, UID_LEN);
        else
            memset(rxConfig.m_config.uid, 0, UID_LEN);

        rxConfig.m_modified = true;
    }
}

bool RxConfig_IsModified(void) 
{ 
    #ifdef SIM_TUBE
    return false;
    #else
    return rxConfig.m_modified; 
    #endif
}

uint8_t RxConfig_GetRateInitialIdx() 
{ 
    #ifdef SIM_TUBE
    return RATE_TMS_250HZ;
    #else
    return rxConfig.m_config.rateInitialIdx; 
    #endif
}

void RxConfig_Init(RxConfig_t *config)
{
    config->SetStorageProvider = RxConfigSetStorageProvider;
    config->Load = RxConfigLoad;
    config->SetDefaults = RxConfigSetDefaults;
    config->ReturnLoan = RxConfigReturnLoan;
    config->IsOnLoan = RxConfigIsOnLoan;
    config->GetIsBound = RxConfigGetIsBound;
    config->Commit = RxConfigCommit;
    config->GetBindStorage = RxConfigGetBindStorage;
    config->GetSerialProtocol = RxConfigGetSerialProtocol;
    config->GetFailsafeMode = RxConfigGetFailsafeMode;
    config->SetUID = RxConfigSetUID;
    config->GetUID = RxConfigGetUID;
    config->GetModelId = RxConfigGetModelId;
    config->GetPower = RxConfigGetPower;
    config->IsModified = RxConfig_IsModified;
    config->SetPowerOnCounter = RxConfig_SetPowerOnCounter;
    config->GetPowerOnCounter = RxConfig_GetPowerOnCounter;
    config->GetRateInitialIdx = RxConfig_GetRateInitialIdx;
    config->SetRateInitialIdx = RxConfig_SetRateInitialIdx;
    config->SetModelId = RxConfig_SetModelId;
    config->SetPower = RxConfig_SetPower;
}
