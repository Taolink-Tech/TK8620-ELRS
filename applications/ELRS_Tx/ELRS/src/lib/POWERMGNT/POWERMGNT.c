#include <stdbool.h>

#include "CRSF.h"
#include "POWERMGNT.h"
#include "common.h"
#include "device.h"
#include "helpers.h"
#include "logging.h"
#include "tx_power.h"

#define TxDefaultPower TX_POWER_100mW

uint8_t powerToCrsfPower(PowerLevels_e power)
{
    // Report calibrated product output power, not the chip drive level.
    switch (power)
    {
    case TX_POWER_10mW: return 1;
    case TX_POWER_25mW: return 2;
    case TX_POWER_50mW: return 8;
    case TX_POWER_100mW: return 3;
    case TX_POWER_250mW: return 7;
    case TX_POWER_500mW: return 4;
    case TX_POWER_1000mW: return 5;
    default:
        return 0;
    }
}

PowerLevels_e crsfpowerToPower(uint8_t crsfpower)
{
    switch (crsfpower)
    {
    case 1: return TX_POWER_10mW;
    case 2: return TX_POWER_25mW;
    case 3: return TX_POWER_100mW;
    case 4: return TX_POWER_500mW;
    case 5: return TX_POWER_1000mW;
    case 6: return TX_POWER_1000mW;
    case 7: return TX_POWER_250mW;
    case 8: return TX_POWER_50mW;
    default:
        return TxDefaultPower;
    }
}

static POWERMGNT_t POWERMGNT = {
    .CurrentPower = TxDefaultPower,
    .FanEnableThreshold = TX_POWER_1000mW,
    .MinPower = TX_POWER_50mW,
    .MaxPower = TX_POWER_1000mW,
};

PowerLevels_e POWERMGNT_incPower(void)
{
    DBGLN("incPwr");
    if (POWERMGNT.CurrentPower < POWERMGNT_getMaxPower())
    {
        POWERMGNT_setPower((PowerLevels_e)((uint8_t)POWERMGNT.CurrentPower + 1U));
    }
    return POWERMGNT.CurrentPower;
}

PowerLevels_e POWERMGNT_decPower(void)
{
    if (POWERMGNT.CurrentPower > POWERMGNT.MinPower)
    {
        POWERMGNT_setPower((PowerLevels_e)((uint8_t)POWERMGNT.CurrentPower - 1U));
    }
    return POWERMGNT.CurrentPower;
}

int8_t POWERMGNT_getPowerIndBm(void)
{
    switch (POWERMGNT.CurrentPower)
    {
    // Conducted measurements show that the PA turns on between -1 and 0 dBm
    // chip drive. The 0 dBm drive point produces about 18.45 dBm (70 mW),
    // while lower drive points do not provide usable product output.
    case TX_POWER_50mW: return 0;
    case TX_POWER_100mW: return 3;
    case TX_POWER_250mW: return 6;
    case TX_POWER_500mW: return 10;
    case TX_POWER_1000mW: return 14;
    default: return 3;
    }
}

void POWERMGNT_init(void)
{
    POWERMGNT.CurrentPower = PWR_COUNT;
    POWERMGNT_setDefaultPower();
}

PowerLevels_e POWERMGNT_getDefaultPower(void)
{
    if (POWERMGNT.MinPower > TxDefaultPower)
    {
        return POWERMGNT.MinPower;
    }
    if (POWERMGNT_getMaxPower() < TxDefaultPower)
    {
        return POWERMGNT_getMaxPower();
    }
    return TxDefaultPower;
}

void POWERMGNT_setDefaultPower(void)
{
    DBGLN("setDefaultPwr");
    POWERMGNT_setPower(POWERMGNT_getDefaultPower());
}

void POWERMGNT_setPower(PowerLevels_e power)
{
    power = constrain(power, POWERMGNT_getMinPower(), POWERMGNT_getMaxPower());
    if (power == POWERMGNT.CurrentPower)
    {
        return;
    }

    DBGLN("POWERMGNT_setPower: %u", power);
    txPowerChanged = true;
    // Keep LinkStats in sync so OTA and the handset report the current TX power.
    CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_TX_Power = powerToCrsfPower(power);
    POWERMGNT.CurrentPower = power;
    devicesTriggerEvent();
}

PowerLevels_e POWERMGNT_currPower(void)
{
    return POWERMGNT.CurrentPower;
}

PowerLevels_e POWERMGNT_getMaxPower(void)
{
    PowerLevels_e power = POWERMGNT.MaxPower;
    return power;
}

PowerLevels_e POWERMGNT_getMinPower(void)
{
    return POWERMGNT.MinPower;
}
