#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "device.h"

extern device_t LUA_device;
extern device_t LUA_TxDevice;

void luadevUpdateFolderNames();
void luadevHandleRxLuaTelemetry(uint8_t *data);
