#pragma once

#include <stdint.h>

#include "config.h"
#include "POWERMGNT.h"
#include "CRSF.h"
#include "logging.h"

#define DYNPOWER_UPDATE_NOUPDATE (-128)
#define DYNPOWER_UPDATE_MISSED   (-127)

// Call DynamicPower_Init in setup()
void DynamicPower_Init(void);
// Call DynamicPower_Update from loop()
void DynamicPower_Update(uint32_t now);
// Call DynamicPower_TelemetryUpdate from ISR (or fast path) with DYNPOWER_UPDATE_MISSED or SNR value
void DynamicPower_TelemetryUpdate(int8_t snr);

