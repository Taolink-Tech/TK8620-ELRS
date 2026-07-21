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
    case TX_POWER_100mW: return 3;
    case TX_POWER_1000mW: return 5;
    default:
        return 0;
    }
}

PowerLevels_e crsfpowerToPower(uint8_t crsfpower)
{
    switch (crsfpower)
    {
    case 3: return TX_POWER_100mW;
    case 4:
    case 5:
    case 6: return TX_POWER_1000mW;
    default:
        return TxDefaultPower;
    }
}

static POWERMGNT_t POWERMGNT = {
    .CurrentPower = TxDefaultPower,
    .FanEnableThreshold = TX_POWER_1000mW,
    .MinPower = TX_POWER_100mW,
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
    case TX_POWER_100mW: return 5;
    case TX_POWER_1000mW: return 20; // Product output measured at 30.5 dBm.
    default: return 5;
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
