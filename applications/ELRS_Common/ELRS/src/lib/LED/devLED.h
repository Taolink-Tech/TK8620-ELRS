#pragma once

#include "device.h"

#if defined(GPIO_PIN_LED_WS2812)
extern device_t RGB_device;
#define HAS_RGB
#endif

extern device_t LED_device;