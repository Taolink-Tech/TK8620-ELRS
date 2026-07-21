#pragma once

#include "device.h"
#include "stubborn_sender.h"

extern device_t Handset_device;

void devHandset_RegisterSendRCdataToRF(void (*cb)(bool isRcData));
