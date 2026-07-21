#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
// #include "CRSF.h"
#include "CRSFHandset.h"
#include "devHandset.h"
#include "handset.h"
#include "OTA.h"
#include "common.h"
#include "tk86xx_api.h"
#include "helpers.h"
#include "logging.h"
#include "config.h"
#include "stubborn_receiver.h"

static void (*pSendRCdataToRF)(bool isRcData) = NULL;
uint8_t BindingSendCount = 0;
//// MSP Data Handling ///////
bool NextPacketIsMspData = false;  // if true the next packet will contain the msp data
Handset_t handset = {
    .controllerConnected = false,
    .RCdataLastRecv = 0,
    .RequestedRCpacketInterval = 5000, // default to 200hz as per 'normal'
    .RCdataCallback = NULL,
    .RecvParameterUpdate = NULL,
    .disconnected = NULL,
    .connected = NULL,
    .RecvModelUpdate = NULL,
    .OnBindingCommand = NULL,
    .Begin = NULL,
    .sendTelemetryToTX = NULL,
    // .End = NULL,
    .handleInput = NULL,
    .registerParameterUpdateCallback = NULL,
    .IsArmed = NULL,
    // .JustSentRFpacket = NULL,
};

static void initialize()
{
    Handset_Init(&handset);
}

static int start()
{
    handset.Begin();
    return DURATION_IMMEDIATELY;
}

static int timeout()
{
    bool isRcData = handset.handleInput();    
    if (pSendRCdataToRF) pSendRCdataToRF(isRcData);
    return DURATION_IMMEDIATELY;
}

static int event()
{
    // An event should be generated every time the TX power changes
    // CRSF_GetLinkStatistics()->crsfLinkStatistics.uplink_TX_Power = powerToCrsfPower(POWERMGNT_currPower());
    return DURATION_IGNORE;
}

device_t Handset_device = {
    .initialize = initialize,
    .start = start,
    .event = event,
    .timeout = timeout
};

void devHandset_RegisterSendRCdataToRF(void (*cb)(bool isRcData))
{
    pSendRCdataToRF = cb;
}

