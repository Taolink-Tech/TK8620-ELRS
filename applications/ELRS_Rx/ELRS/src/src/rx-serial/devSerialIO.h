#pragma once

#include "device.h"

extern device_t Serial0_device;
extern void handleSerialIO();
extern void crsfRCFrameAvailable();
extern void crsfRCFrameMissed();
