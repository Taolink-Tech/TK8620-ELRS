#pragma once

#include "POWERMGNT.h"

// Product output-power levels shown on the handset and stored in TX config.
// Chip drive power is mapped centrally in POWERMGNT_getPowerIndBm().
#define TX_POWER_10mW   ((PowerLevels_e)0U)
#define TX_POWER_25mW   ((PowerLevels_e)1U)
#define TX_POWER_50mW   ((PowerLevels_e)2U)
#define TX_POWER_100mW  ((PowerLevels_e)3U)
#define TX_POWER_250mW  ((PowerLevels_e)4U)
#define TX_POWER_500mW  ((PowerLevels_e)5U)
#define TX_POWER_1000mW ((PowerLevels_e)6U)
#define TX_POWER_COUNT  7U
