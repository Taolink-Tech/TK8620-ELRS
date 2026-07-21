#include <stdbool.h>

#include "POWERMGNT.h"
#include "common.h"
#include "device.h"
#include "helpers.h"
#include "logging.h"
#include "tk86xx_api.h"

uint8_t powerToCrsfPower(PowerLevels_e power)
{
    // Crossfire uses a non-linear power enum; preserve that mapping here.
    switch (power)
    {
    case PWR_10mW: return 1;
    case PWR_25mW: return 2;
    case PWR_50mW: return 8;
    case PWR_100mW: return 3;
    case PWR_250mW: return 7;
    case PWR_500mW: return 4;
    case PWR_1000mW: return 5;
    case PWR_2000mW: return 6;
    default:
        return 0;
    }
}

PowerLevels_e crsfpowerToPower(uint8_t crsfpower)
{
    switch (crsfpower)
    {
    case 1: return PWR_10mW;
    case 2: return PWR_25mW;
    case 3: return PWR_100mW;
    case 4: return PWR_500mW;
    case 5: return PWR_1000mW;
    case 6: return PWR_2000mW;
    case 7: return PWR_250mW;
    case 8: return PWR_50mW;
    default:
        return PWR_10mW;
    }
}

static POWERMGNT_t POWERMGNT = {
    .CurrentPower = PWR_100mW,
    .FanEnableThreshold = PWR_250mW,
    .MinPower = PWR_10mW,
    // TK8620 output power tops out at 20 dBm, so clamp the public power ladder
    // to 100 mW for this target.
    .MaxPower = PWR_100mW,
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
    DBGLN("decPwr");
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
    case PWR_10mW: return 10;
    case PWR_25mW: return 14;
    case PWR_50mW: return 17;
    case PWR_100mW: return 20;
    case PWR_250mW: return 24;
    case PWR_500mW: return 27;
    case PWR_1000mW: return 30;
    case PWR_2000mW: return 33;
    default:
        return 0;
    }
}

void POWERMGNT_init(void)
{
#if SENSI_TEST
    POWERMGNT.CurrentPower = POWERMGNT_getDefaultPower();
#else
    POWERMGNT.CurrentPower = PWR_COUNT;
    POWERMGNT_setDefaultPower();
#endif
}

PowerLevels_e POWERMGNT_getDefaultPower(void)
{
    if (POWERMGNT.MinPower > DefaultPower)
    {
        return POWERMGNT.MinPower;
    }
    if (POWERMGNT_getMaxPower() < DefaultPower)
    {
        return POWERMGNT_getMaxPower();
    }
    return DefaultPower;
}

void POWERMGNT_setDefaultPower(void)
{
    DBGLN("setDefaultPwr");
    POWERMGNT_setPower(POWERMGNT_getDefaultPower());
}

void POWERMGNT_setPower(PowerLevels_e power)
{
#if SENSI_TEST
    return;
#endif

    power = constrain(power, POWERMGNT_getMinPower(), POWERMGNT_getMaxPower());
    if (power == POWERMGNT.CurrentPower)
    {
        return;
    }

    DBGLN("POWERMGNT_setPower: %u", power);
    txPowerChanged = true;
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
#if defined(Regulatory_Domain_EU_CE_2400)
    if (power > PWR_100mW)
    {
        power = PWR_100mW;
    }
#endif
    return power;
}

PowerLevels_e POWERMGNT_getMinPower(void)
{
    return POWERMGNT.MinPower;
}
