#pragma once
#include <stdbool.h>
#include "tk86xx_api.h"
#include "device.h"

extern device_t ratioRxDevice;

void DevRadioRx_RegisterRxSlotResultCb(void (*cb)(SignalQuality_t *signalQuality, bool isRxDataSlot, bool hasPayload));
void DevRadioRx_RegisterTxDoneCb(void (*cb)(void));

// Switch to a new air-rate index and restart RX radio.
void DevRadioRx_RequestAirRateChange(uint8_t newRateIndex);
