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
#include "serial_port.h"
#include "tk86xx_api.h"

#define BIT(a)               (1 << (a))
#define MODEL_CHANGED       BIT(1)
#define VTX_CHANGED         BIT(2)
#define MAIN_CHANGED        BIT(3) // catch-all for global config item
#define FAN_CHANGED         BIT(4)
#define MOTION_CHANGED      BIT(5)
#define BUTTON_CHANGED      BIT(6)
#define ALL_CHANGED         (MODEL_CHANGED | VTX_CHANGED | MAIN_CHANGED | FAN_CHANGED | MOTION_CHANGED | BUTTON_CHANGED)

static uint8_t TxConfig_ClampSupportedRate(uint8_t rate)
{
    return (rate >= RATE_TMS_25HZ && rate < RATE_MAX) ? rate : RATE_TMS_250HZ;
}

static void TxConfigLoad(void)
{
    txConfig.m_modified = 0;
    txConfig.m_eeprom->Get(0, (uint8_t*)&txConfig.m_config, sizeof(txConfig.m_config));
    uint32_t version = 0;
    if ((txConfig.m_config.version & CONFIG_MAGIC_MASK) == TX_CONFIG_MAGIC)
        version = txConfig.m_config.version & ~CONFIG_MAGIC_MASK;

    // If version is current, all done
    if (version == TX_CONFIG_VERSION) {
        return;
    }

    // Can't upgrade from version <5, or when flashing a previous version, just use defaults.
    if (version < 5 || version > TX_CONFIG_VERSION)
    {
        txConfig.SetDefaults(true);
        return;
    }

    // Upgrade EEPROM, starting with defaults
    txConfig.SetDefaults(false);
}

static void TxConfig_Commit()
{
    if (!txConfig.m_modified)
    {
        // No changes
        return;
    }

    // Write the struct to eeprom
    txConfig.m_eeprom->Put(0, &txConfig.m_config);
    txConfig.m_eeprom->Commit(0, (uint8_t*)&txConfig.m_config, sizeof(txConfig.m_config));
    txConfig.m_modified = 0;
}

void TxConfig_SetRate(uint8_t rate)
{
    rate = TxConfig_ClampSupportedRate(rate);
    if (txConfig.m_model->rate != rate)
    {
        txConfig.m_model->rate = rate;
        txConfig.m_modified |= MODEL_CHANGED;
    }
}

void TxConfig_SetTlm(uint8_t tlm)
{
    if (txConfig.GetTlm() != tlm)
    {
        txConfig.m_model->tlm = tlm;
        txConfig.m_modified |= MODEL_CHANGED;
    }
}

static void TxConfig_SetPower(uint8_t power)
{
    if (txConfig.GetPower() != power)
    {
        txConfig.m_model->power = power;
        txConfig.m_modified |= MODEL_CHANGED;
    }
}

static void TxConfig_SetDynamicPower(bool dynamicPower)
{
    if (txConfig.GetDynamicPower() != dynamicPower)
    {
        txConfig.m_model->dynamicPower = dynamicPower ? 1U : 0U;
        txConfig.m_modified |= MODEL_CHANGED;
    }
}

static void TxConfig_SetBoostChannel(uint8_t boostChannel)
{
    // 0=disabled, 1..4 maps to AUX9..AUX12
    if (boostChannel > 4U) boostChannel = 4U;
    if (txConfig.GetBoostChannel() != boostChannel)
    {
        txConfig.m_model->boostChannel = boostChannel;
        txConfig.m_modified |= MODEL_CHANGED;
    }
}

void TxConfig_SetSwitchMode(uint8_t switchMode)
{
    if (txConfig.GetSwitchMode() != switchMode)
    {
        txConfig.m_model->switchMode = switchMode;
        txConfig.m_modified |= MODEL_CHANGED;
    }
}

void TxConfig_SetModelMatch(bool modelMatch)
{
    if (txConfig.GetModelMatch() != modelMatch)
    {
        txConfig.m_model->modelMatch = modelMatch;
        txConfig.m_modified |= MODEL_CHANGED;
    }
}

void TxConfigSetStorageProvider(ELRS_EEPROM_t *eeprom)
{
    if (eeprom) txConfig.m_eeprom = eeprom;
}

static void TxConfigSetDefaults(bool commit)
{
    // Reset everything to 0/false and then just set anything that zero is not appropriate
    memset(&txConfig.m_config, 0, sizeof(txConfig.m_config));

    txConfig.m_config.version = TX_CONFIG_VERSION | TX_CONFIG_MAGIC;
    // txConfig.m_config.powerFanThreshold = PWR_250mW;
    txConfig.m_config.crsf_serial_baud_enum = CRSF_SERIAL_BAUD_400K;
    txConfig.m_modified = ALL_CHANGED;

    if (commit)
    {
        txConfig.m_modified = ALL_CHANGED;
    }

    // Set defaults for button 1
    tx_button_color_t default_actions1 = {
        .val = {
            .color = 226,   // R:255 G:0 B:182
            .actions = {
                {false, 2, ACTION_BIND},
                {true, 0, ACTION_INCREASE_POWER}
            }
        }
    };
    txConfig.m_config.buttonColors[0].raw = default_actions1.raw;

    // Set defaults for button 2
    tx_button_color_t default_actions2 = {
        .val = {
            .color = 3,     // R:0 G:0 B:255
            .actions = {
                {false, 1, ACTION_GOTO_VTX_CHANNEL},
                {true, 0, ACTION_SEND_VTX}
            }
        }
    };
    txConfig.m_config.buttonColors[1].raw = default_actions2.raw;

    for (unsigned i=0; i<CONFIG_TX_MODEL_CNT; i++)
    {
        txConfig.SetModelId(i);
        txConfig.m_model->rate = RATE_TMS_250HZ;
        txConfig.m_model->tlm = TLM_RATIO_STD;
        txConfig.m_model->power = POWERMGNT_getDefaultPower();
    }

#if !defined(PLATFORM_ESP32)
    // STM32/ESP8266 just needs one commit
    if (commit)
    {
        txConfig.m_eeprom->Commit(0, (uint8_t*)&txConfig.m_config, sizeof(txConfig.m_config));
    }
#endif

    txConfig.SetModelId(0);
    txConfig.m_modified = 0;
}

static uint8_t TxConfig_GetSwitchMode() 
{ 
    #ifdef SIM_TUBE
    return 0;
    #else
    return txConfig.m_model->switchMode; 
    #endif
}

static uint8_t TxConfig_GetRate() 
{ 
    #ifdef SIM_TUBE
    return 7;
    #else
    return TxConfig_ClampSupportedRate(txConfig.m_model->rate);
    #endif
}

/**
 * Sets ModelId used for subsequent per-model config gets
 * Returns: true if the model has changed
 **/
bool TxConfig_SetModelId(uint8_t modelId)
{
    model_config_t *newModel = &txConfig.m_config.model_config[modelId];
    if (newModel != txConfig.m_model)
    {
        txConfig.m_model = newModel;
        txConfig.m_modelId = modelId;
        return true;
    }

    return false;
}

static uint8_t TxConfig_GetLinkMode() 
{ 
    #ifdef SIM_TUBE
    return 0;
    #else
    return txConfig.m_model->linkMode; 
    #endif
}

static bool TxConfig_GetBackpackDisable() 
{ 
    return txConfig.m_config.backpackDisable; 
}

static uint8_t TxConfig_GetTlm() 
{ 
    #ifdef SIM_TUBE
    return TLM_RATIO_STD;
    #else
    return txConfig.m_model->tlm; 
    #endif
}

static bool TxConfig_GetDynamicPower() 
{ 
    #ifdef SIM_TUBE
    return false;
    #else
    return txConfig.m_model->dynamicPower; 
    #endif
}

static uint8_t TxConfig_GetBoostChannel() 
{ 
    return txConfig.m_model->boostChannel; 
}

static uint8_t TxConfig_GetPower() 
{ 
    #ifdef SIM_TUBE
    return (uint8_t)POWERMGNT_getDefaultPower();
    #else
    return txConfig.m_model->power; 
    #endif
}

static uint8_t TxConfig_GetVtxBand() 
{ 
    return txConfig.m_config.vtxBand; 
}

static uint8_t TxConfig_GetVtxChannel() 
{ 
    return txConfig.m_config.vtxChannel; 
}

static uint8_t TxConfig_GetCrsfSerialBaudEnum()
{
    // Clamp to valid range to avoid using garbage if EEPROM contains older data
    // or corrupted bytes. Default to 400k.
    #ifdef SIM_TUBE
    return CRSF_SERIAL_BAUD_400K;
    #else
    const uint8_t v = txConfig.m_config.crsf_serial_baud_enum;
    return (v < CRSF_SERIAL_BAUD_MAX) ? v : CRSF_SERIAL_BAUD_400K;
    #endif
}

static void TxConfig_SetCrsfSerialBaudEnum(uint8_t baudEnum)
{
    if (baudEnum >= CRSF_SERIAL_BAUD_MAX) {
        baudEnum = CRSF_SERIAL_BAUD_420K;
    }
    if (txConfig.m_config.crsf_serial_baud_enum != baudEnum)
    {
        txConfig.m_config.crsf_serial_baud_enum = baudEnum;
        txConfig.m_modified |= MODEL_CHANGED;
    }
}

static bool TxConfigIsModified() 
{ 
    return txConfig.m_modified; 
}

static bool TxConfig_GetModelMatch() 
{ 
    #ifdef SIM_TUBE
    return true;
    #else
    return txConfig.m_model->modelMatch;
    #endif
}

void TxConfig_Init(TxConfig_t *config)
{
    config->m_model = &config->m_config.model_config[0];
    config->SetStorageProvider = TxConfigSetStorageProvider;
    config->Load = TxConfigLoad;
    config->SetDefaults = TxConfigSetDefaults;
    config->GetSwitchMode = TxConfig_GetSwitchMode;
    config->SetSwitchMode = TxConfig_SetSwitchMode;
    config->GetRate = TxConfig_GetRate;
    config->SetRate = TxConfig_SetRate;
    config->GetLinkMode = TxConfig_GetLinkMode;
    config->GetBackpackDisable = TxConfig_GetBackpackDisable;
    config->GetTlm = TxConfig_GetTlm;
    config->SetTlm = TxConfig_SetTlm;
    config->GetDynamicPower = TxConfig_GetDynamicPower;
    config->GetBoostChannel = TxConfig_GetBoostChannel;
    config->GetPower = TxConfig_GetPower;
    config->SetPower = TxConfig_SetPower;
    config->SetDynamicPower = TxConfig_SetDynamicPower;
    config->SetBoostChannel = TxConfig_SetBoostChannel;
    config->GetVtxBand = TxConfig_GetVtxBand;
    config->GetVtxChannel = TxConfig_GetVtxChannel;
    config->GetCrsfSerialBaudEnum = TxConfig_GetCrsfSerialBaudEnum;
    config->IsModified = TxConfigIsModified;
    config->Commit = TxConfig_Commit;
    config->SetModelId = TxConfig_SetModelId;
    config->GetModelMatch = TxConfig_GetModelMatch;
    config->SetModelMatch = TxConfig_SetModelMatch;
    config->SetCrsfSerialBaudEnum = TxConfig_SetCrsfSerialBaudEnum;
}
