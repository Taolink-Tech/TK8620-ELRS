#pragma once

#include <stdbool.h>
#include "elrs_eeprom.h"
// #include "options.h"
#include "common.h"
#include "tk86xx_api.h"
#include "flash_hal.h"

#define POWERON_CNT_OFFSET (0x1000)

// CONFIG_MAGIC is ORed with CONFIG_VERSION in the version field
#define CONFIG_MAGIC_MASK   (0b11U << 30)
#define TX_CONFIG_MAGIC     (0b01U << 30)
#define RX_CONFIG_MAGIC     (0b10U << 30)

#define RX_CONFIG_VERSION   9U

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


typedef enum {
    BACKPACK_TELEM_MODE_OFF,
    BACKPACK_TELEM_MODE_ESPNOW,
    BACKPACK_TELEM_MODE_WIFI,
    BACKPACK_TELEM_MODE_BLUETOOTH,
} telem_mode_t;

///////////////////////////////////////////////////

// const uint8_t PWM_MAX_CHANNELS = 16;

typedef enum {
    BINDSTORAGE_PERSISTENT = 0,
    BINDSTORAGE_VOLATILE = 1,
    BINDSTORAGE_RETURNABLE = 2,
    BINDSTORAGE_ADMINISTERED = 3,
} rx_config_bindstorage_t;

typedef union {
    struct {
        uint32_t failsafe:10,    // us output during failsafe +988 (e.g. 512 here would be 1500us)
                 inputChannel:4, // 0-based input channel
                 inverted:1,     // invert channel output
                 mode:4,         // Output mode (eServoOutputMode)
                 narrow:1,       // Narrow output mode (half pulse width)
                 failsafeMode:2, // failsafe output mode (eServoOutputFailsafeMode)
                 unused:10;      // FUTURE: When someone complains "everyone" uses inverted polarity PWM or something :/
    } val;
    uint32_t raw;
} rx_config_pwm_t;

typedef struct __attribute__((packed)) {
    uint32_t    version;
    uint8_t     uid[UID_LEN];
    uint8_t     unused_padding;
    uint8_t     serial1Protocol:4,  // secondary serial protocol
                serial1Protocol_unused:4;
    uint32_t    flash_discriminator;
    struct __attribute__((packed)) {
        uint16_t    scale;          // FUTURE: Override compiled vbat scale
        int16_t     offset;         // FUTURE: Override comiled vbat offset
    } vbat;
    uint8_t     bindStorage:2,     // rx_config_bindstorage_t
                power:4,
                antennaMode:2;      // 0=0, 1=1, 2=Diversity
    uint8_t     powerOnCounter:3,
                forceTlmOff:1,
                rateInitialIdx:4;   // Rate to start rateCycling at on boot
    uint8_t     modelId;
    uint8_t     serialProtocol:4,
                failsafeMode:2,
                unused:2;
    // rx_config_pwm_t pwmChannels[PWM_MAX_CHANNELS] __attribute__((aligned(4)));
    uint8_t     teamraceChannel:4,
                teamracePosition:3,
                teamracePitMode:1;  // FUTURE: Enable pit mode when disabling model
    uint8_t     targetSysId;
    uint8_t     sourceSysId;
} rx_config_t;

typedef struct {
    void (*Load)(void);
    void (*Commit)(void);

    bool     (*GetIsBound)(void);
    const uint8_t* (*GetUID)(void);
    const uint8_t  (*GetPowerOnCounter)(void);
    uint8_t  (*GetModelId)(void);
    uint8_t  (*GetPower)(void);
    bool (*IsModified)(void);
    uint8_t (*GetRateInitialIdx)(void);
    eSerialProtocol_e (*GetSerialProtocol)(void); 
    enum eFailsafeMode (*GetFailsafeMode)(void);
    rx_config_bindstorage_t (*GetBindStorage)(void);
    bool (*IsOnLoan)(void);

    void (*SetUID)(uint8_t* uid);
    void (*SetPowerOnCounter)(uint8_t powerOnCounter);
    void (*SetModelId)(uint8_t modelId);
    void (*SetPower)(uint8_t power);
    void (*SetDefaults)(bool commit);
    void (*SetStorageProvider)(ELRS_EEPROM_t *eeprom);
//     #if defined(GPIO_PIN_PWM_OUTPUTS)
//     void SetPwmChannel(uint8_t ch, uint16_t failsafe, uint8_t inputCh, bool inverted, uint8_t mode, bool narrow);
//     void SetPwmChannelRaw(uint8_t ch, uint32_t raw);
//     #endif
//     void SetForceTlmOff(bool forceTlmOff);
    void (*SetRateInitialIdx)(uint8_t rateInitialIdx);
    void (*SetSerialProtocol)(eSerialProtocol_e serialProtocol);
// #if defined(PLATFORM_ESP32)
//     void SetSerial1Protocol(eSerial1Protocol serial1Protocol);
// #endif
//     void SetTeamraceChannel(uint8_t teamraceChannel);
//     void SetTeamracePosition(uint8_t teamracePosition);
//     void SetFailsafeMode(eFailsafeMode failsafeMode);
//     void SetTargetSysId(uint8_t sysID);
//     void SetSourceSysId(uint8_t sysID);
//     void SetBindStorage(rx_config_bindstorage_t value);
    void (*ReturnLoan)(void);

// private:
    // void (*CheckUpdateFlashedUid)(bool skipDescrimCheck);
//     void UpgradeUid(uint8_t *onLoanUid, uint8_t *boundUid);
//     void UpgradeEepromV4();
//     void UpgradeEepromV5();
//     void UpgradeEepromV6();
//     void UpgradeEepromV7V8();

    rx_config_t m_config;
    ELRS_EEPROM_t *m_eeprom;
    bool        m_modified;
} RxConfig_t;

extern RxConfig_t rxConfig;

void RxConfig_Init(RxConfig_t *config);

