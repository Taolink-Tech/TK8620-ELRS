#include "dynpower.h"

#include <stdint.h>

#include "config.h"
#include "POWERMGNT.h"
#include "CRSF.h"
#include "logging.h"

void DynamicPower_UpdateRx(bool initialize)
{
    const uint8_t cfgPower = rxConfig.GetPower();

    if (cfgPower != (uint8_t)PWR_MATCH_TX)
    {
        POWERMGNT_setPower((PowerLevels_e)cfgPower);
        return;
    }

    if (CRSF_clearUpdatedUplinkPower())
    {
        PowerLevels_e newPower = crsfpowerToPower(CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_TX_Power);
        DBGLN("Matching TX power %u", (unsigned)newPower);
        POWERMGNT_setPower(newPower);
    }
    else if (initialize)
    {
        POWERMGNT_setPower(POWERMGNT_getDefaultPower());
    }
}

