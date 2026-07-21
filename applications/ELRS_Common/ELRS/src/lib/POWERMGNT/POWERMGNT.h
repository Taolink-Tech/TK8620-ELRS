#pragma once

#include <stdint.h>
#include <stddef.h>
#include "options.h"

#if defined(PLATFORM_ESP32)
#include <nvs_flash.h>
#include <nvs.h>
#endif

#ifndef POWER_OUTPUT_VALUES
    // These are "fake" values as the power on the RX is not user selectable
#endif

#if !defined(DefaultPower)
    #define DefaultPower PWR_100mW
#endif

typedef enum
{
    PWR_0p1mW = 0, // -10 dBm
    PWR_1mW = 1, // 0 dBm
    PWR_3mW = 2, // 5 dBm (3.16 mW nominal)
    PWR_10mW = 3, // 10 dBm
    PWR_25mW = 4, // 13.98 dBm, 14dBm
    PWR_50mW = 5, // 16.99dBm, 17 dBm
    PWR_100mW = 6, // 20 dBm
    PWR_250mW = 7, // 23.98dBm, 24 dBm
    PWR_500mW = 8, // 26.99dBm, 27 dBm
    PWR_1000mW = 9, // 30 dBm
    PWR_2000mW = 10, // 33.0133dBm, 33 dBm
    PWR_COUNT = 11,
    PWR_MATCH_TX = PWR_COUNT,
} PowerLevels_e;

uint8_t powerToCrsfPower(PowerLevels_e Power);
PowerLevels_e crsfpowerToPower(uint8_t crsfpower);

typedef struct {
    PowerLevels_e CurrentPower;
    PowerLevels_e FanEnableThreshold;
    PowerLevels_e MinPower;
    PowerLevels_e MaxPower;
} POWERMGNT_t;

#define CALIBRATION_MAGIC    0x43414C << 8   //['C', 'A', 'L']
#define CALIBRATION_VERSION   1

/**
* @brief Increment to the next higher power level, capped at MaxPower
*
* @return PowerLevels_e the new power level
*/
PowerLevels_e POWERMGNT_incPower();

/**
* @brief Decrement to the next lower power level, capped at MinPower
*
* @return PowerLevels_e the new power level
*/
PowerLevels_e POWERMGNT_decPower();

/**
* @brief Get the currently configured power level in dBm
*
* @return int8_t the dBm for the current power level
*/
int8_t POWERMGNT_getPowerIndBm();

/**
* @brief Initialise the power management subsystem.
* Configures PWM ouptut pins, DACs, loads power calibration settings
* and sets output power to the default power level as appropriate for the
* device
*/
void POWERMGNT_init();

/**
* @brief Get the Default power level for this device
*
* @return PowerLevels_e the default power level
*/
PowerLevels_e POWERMGNT_getDefaultPower();

/**
* @brief Set the output power to the default power level
*/
void POWERMGNT_setDefaultPower();

/**
* @brief Set the power level, constrained to MinPower..MaxPower
*
* @param Power the power level to set
*/
void POWERMGNT_setPower(PowerLevels_e Power);

/**
* @brief Get the currently selected power level
*
* @return PowerLevels_e the currently selected power level
*/
PowerLevels_e POWERMGNT_currPower();

/**
* @brief Get the MaxPower level supported by this device.
* For devices that support the HighPower override, i.e. R9M with the fan hack,
* the MaxPower is normally HighPower unless the 'unlock_higher_power' option
* is set at compile time.
*
* @return PowerLevels_e the maximum power level supported
*/
PowerLevels_e POWERMGNT_getMaxPower();

/**
* @brief Get the MinPower level supported by this device
*
* @return PowerLevels_e the minimum power level supported
*/
PowerLevels_e POWERMGNT_getMinPower();
