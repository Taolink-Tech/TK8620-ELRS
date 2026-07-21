#pragma once

#include "POWERMGNT.h"

// Product output-power levels. The numeric values are the two consecutive
// storage slots used by this TX target; chip drive power is mapped centrally
// in POWERMGNT_getPowerIndBm().
#define TX_POWER_100mW  PWR_100mW
#define TX_POWER_1000mW PWR_250mW
