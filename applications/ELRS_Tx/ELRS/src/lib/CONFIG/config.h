#pragma once

#include <stdbool.h>
#include "elrs_eeprom.h"
// #include "options.h"
#include "common.h"
#include "tk86xx_api.h"
#include "flash_hal.h"
#include "serial_port.h"

#define POWERON_CNT_OFFSET (0x1000)

// CONFIG_MAGIC is ORed with CONFIG_VERSION in the version field
#define CONFIG_MAGIC_MASK   (0b11U << 30)
#define TX_CONFIG_MAGIC     (0b01U << 30)
#define RX_CONFIG_MAGIC     (0b10U << 30)

#define TX_CONFIG_VERSION   7U
#define RX_CONFIG_VERSION   9U

#define CONFIG_TX_BUTTON_ACTION_CNT 2
#define CONFIG_TX_MODEL_CNT         64

typedef struct {
    uint32_t    rate:4,
                tlm:4,
                power:3,
                switchMode:2,
                boostChannel:3, // dynamic power boost AUX channel
                dynamicPower:1,
                modelMatch:1,
                txAntenna:2,    // FUTURE: Which TX antenna to use, 0=Auto
                ptrStartChannel:4,
                ptrEnableChannel:5,
                linkMode:3;
} model_config_t;

typedef struct {
    uint8_t     pressType:1,    // 0 short, 1 long
                count:3,        // 1-8 click count for short, .5sec hold count for long
                action:4;       // action to execute
} button_action_t;

typedef union {
    struct {
        uint8_t color;                  // RRRGGGBB
        button_action_t actions[CONFIG_TX_BUTTON_ACTION_CNT];
        uint8_t unused;
    } val;
    uint32_t raw;
} tx_button_color_t;

typedef enum {
    BACKPACK_TELEM_MODE_OFF,
    BACKPACK_TELEM_MODE_ESPNOW,
    BACKPACK_TELEM_MODE_WIFI,
    BACKPACK_TELEM_MODE_BLUETOOTH,
} telem_mode_t;

typedef struct {
    uint32_t        version;
    uint8_t         vtxBand;    // 0=Off, else band number
    uint8_t         vtxChannel; // 0=Ch1 -> 7=Ch8
    uint8_t         vtxPower;   // 0=Do not set, else power number
    uint8_t         vtxPitmode; // Off/On/AUX1^/AUX1v/etc
    uint8_t         powerFanThreshold:4;     // Power level to enable fan if present
    uint8_t         crsf_serial_baud_enum:3; // 0:400000(default) 1:420000 2:921600
    uint8_t         _unused0:1;
    model_config_t  model_config[CONFIG_TX_MODEL_CNT];
    uint8_t         fanMode;            // some value used by thermal?
    uint8_t         motionMode:2,       // bool, but space for 2 more modes
                    dvrStopDelay:3,
                    backpackDisable:1,  // bool, disable backpack via EN pin if available
                    backpackTlmMode:2;  // 0=Off, 1=Fwd tlm via espnow, 2=fwd tlm via wifi 3=(FUTURE) bluetooth
    uint8_t         dvrStartDelay:3,
                    dvrAux:5;
    tx_button_color_t buttonColors[2];  // FUTURE: TX RGB color / mode (sets color of TX, can be a static color or standard)
                                        // FUTURE: Model RGB color / mode (sets LED color mode on the model, but can be second TX led color too)
                                        // FUTURE: Custom button actions
} tx_config_t;

typedef struct {
    void (*Load)(void);
    void (*Commit)(void);

    uint8_t (*GetRate)(void);
    uint8_t (*GetTlm)(void);
    uint8_t (*GetPower)(void);
    bool (*GetDynamicPower)(void);
    uint8_t (*GetBoostChannel)(void);
    uint8_t (*GetSwitchMode)(void);
    uint8_t (*GetLinkMode)(void);
    bool (*GetModelMatch)(void);
    bool     (*IsModified)(void);
    uint8_t (*GetVtxBand)(void);
    uint8_t (*GetVtxChannel)(void);
    uint8_t (*GetCrsfSerialBaudEnum)(void);
    bool     (*GetBackpackDisable)(void);

    void (*SetRate)(uint8_t rate);
    void (*SetTlm)(uint8_t tlm);
    void (*SetPower)(uint8_t power);
    void (*SetDynamicPower)(bool dynamicPower);
    void (*SetBoostChannel)(uint8_t boostChannel);
    void (*SetSwitchMode)(uint8_t switchMode);
    void (*SetModelMatch)(bool modelMatch);
    void (*SetCrsfSerialBaudEnum)(uint8_t baudEnum);
    void (*SetDefaults)(bool commit);
    void (*SetStorageProvider)(ELRS_EEPROM_t *eeprom);
//     void SetBackpackDisable(bool backpackDisable);
//     void SetBackpackTlmMode(uint8_t mode);
//     void SetPTRStartChannel(uint8_t ptrStartChannel);
//     void SetPTREnableChannel(uint8_t ptrEnableChannel);

    // State setters
    bool (*SetModelId)(uint8_t modelId);

// private:
// #if !defined(PLATFORM_ESP32)
//     void UpgradeEepromV5ToV6();
//     void UpgradeEepromV6ToV7();
// #endif

    tx_config_t m_config;
    ELRS_EEPROM_t *m_eeprom;
    volatile uint8_t     m_modified;
    model_config_t *m_model;
    uint8_t     m_modelId;
#if defined(PLATFORM_ESP32)
    nvs_handle  handle;
#endif
} TxConfig_t;

extern TxConfig_t txConfig;
void TxConfig_Init(TxConfig_t *config);




