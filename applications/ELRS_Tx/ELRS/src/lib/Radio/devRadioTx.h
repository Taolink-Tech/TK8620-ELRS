#pragma once

#include "device.h"
#include "tk86xx_api.h"

extern device_t ratioTxDevice;
extern uint8_t BindingSendCount;

void DevRadioTx_RegisterRxDoneCb(void (*cb)(uint8_t *data, uint16_t data_len, SignalQuality_t *signalQuality));
void DevRadioTx_RegisterTxDoneCb(void (*cb)(void));
void DevRadioTx_RegisterTxAbortCb(void (*cb)(void));
void DevRadioTx_RegisterTlmWindowDoneCb(void (*cb)(void));
void DevRadioTx_Stop(void);

// Trigger a coordinated TX/RX packet-rate switch (air rate) via MSP uplink and then restart the radio.
void DevRadioTx_RequestAirRateChange(uint8_t newRateIndex);
void DevRadioTx_RequestTlmRatioChange(uint8_t previousTlmDenom);
