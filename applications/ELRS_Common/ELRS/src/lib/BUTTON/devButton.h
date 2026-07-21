#pragma once

#include <stdbool.h>
#include "device.h"
#include "common.h"

typedef void (*ButtonAction_fn)();

extern device_t Button_device;
#define HAS_BUTTON

typedef struct action {
    uint8_t button;
    bool longPress;
    uint8_t count;
    action_e action;
} action_t;

void registerButtonFunction(action_e action, ButtonAction_fn function);
