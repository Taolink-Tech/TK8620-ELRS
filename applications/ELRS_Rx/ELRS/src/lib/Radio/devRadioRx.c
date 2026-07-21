#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "devRadioRx.h"
#include "device.h"
#include "common.h"
#include "helpers.h"
#include "FHSS.h"
#include "crc.h"
#include "logging.h"
#include "tk86xx_api.h"
#include "POWERMGNT.h"

#define SET_SLOT_MAX         (25)

#ifndef RSSI_COMP_DB
#define RSSI_COMP_DB (-17)
#endif

static bool radioInitFlag = false;
static void (*rxSlotResultCb)(SignalQuality_t *signalQuality, bool isRxDataSlot, bool hasPayload) = NULL;
static void (*txDoneCb)(void) = NULL;

static volatile bool s_pendingAirRateChange = false;
static uint8_t       s_pendingAirRateIndex = 0;

#if SENSI_TEST
static void queueSensiTestPacket(void)
{
    static uint8_t seq = 0;
    uint8_t payload[BBU_TRX_MAX];
    uint16_t len = ExpressLRS_currAirRate_Modparams ? ExpressLRS_currAirRate_Modparams->PayloadLength : TK_DUMMY_PAYLOAD_LEN;

    if (len > sizeof(payload)) {
        len = sizeof(payload);
    }

    payload[0] = 0xA6;
    payload[1] = seq++;
    for (uint16_t i = 2; i < len; i++) {
        payload[i] = (uint8_t)(0x50U + i + seq);
    }

    Tk86xxSendData(payload, len);
}
#endif

static const char *airRateIndexToStr(uint8_t idx)
{
    switch (idx) {
    case 0: return "5Hz";
    case 1: return "10Hz";
    case 2: return "16.6Hz";
    case 3: return "25Hz";
    case 4: return "50Hz";
    case 5: return "100Hz";
    case 6: return "200Hz";
    case 7: return "250Hz";
    default: return "unknown";
    }
}

static int getRssiCompensationDb(void)
{
    if ((ExpressLRS_currAirRate_Modparams != NULL) &&
        ((ExpressLRS_currAirRate_Modparams->enum_rate == RATE_TMS_200HZ) ||
         (ExpressLRS_currAirRate_Modparams->enum_rate == RATE_TMS_250HZ))) {
        return RSSI_COMP_DB;
    }

    return RSSI_COMP_DB + 2;
}

static void applyRssiCompensation(SignalQuality_t *signalQuality)
{
    if ((signalQuality == NULL) || (signalQuality->rssi == 0)) {
        return;
    }

    signalQuality->rssi += getRssiCompensationDb();
}

void DevRadioRx_RegisterRxSlotResultCb(void (*cb)(SignalQuality_t *signalQuality, bool isRxDataSlot, bool hasPayload))
{
    rxSlotResultCb = cb;
}

void DevRadioRx_RegisterTxDoneCb(void (*cb)(void))
{
    txDoneCb = cb;
}

void DevRadioRx_RequestAirRateChange(uint8_t newRateIndex)
{
    if (newRateIndex >= RATE_MAX) {
        newRateIndex = (RATE_MAX > 0) ? (RATE_MAX - 1) : 0;
    }
    s_pendingAirRateIndex = newRateIndex;
    ExpressLRS_currAirRate_Modparams = get_elrs_airRateConfig(s_pendingAirRateIndex);
    if (ExpressLRS_currAirRate_Modparams) {
        DBGLN("[AIRRATE][RX] request idx=%u(%s) interval_us=%lu payload=%u", (unsigned)s_pendingAirRateIndex,
              airRateIndexToStr(s_pendingAirRateIndex),
              (unsigned long)ExpressLRS_currAirRate_Modparams->interval,
              (unsigned)ExpressLRS_currAirRate_Modparams->PayloadLength);
    } else {
        DBGLN("[AIRRATE][RX] request idx=%u(%s) mod=NULL", (unsigned)s_pendingAirRateIndex, airRateIndexToStr(s_pendingAirRateIndex));
    }
    s_pendingAirRateChange = true;
}

static void initialize()
{
    APIRet ret = API_FAILED;

    InitCfg initCfg = {0};

    if (InBindingMode) {
        initCfg.slotNum    = 128 + 1;
    } else {
        #if SENSI_TEST
        initCfg.slotNum    = SENSI_SLOT_NUM;
        #else
        initCfg.slotNum    = 720000 + 1;
        #endif
    }
    initCfg.rf_pwr = (TxPower)POWERMGNT_getPowerIndBm();
    Tk86xxCloseRadio();
    if ((ret = Tk86xxInit(&initCfg)) == API_SUCCESS) {
        SlotCfg slotCfg = {0};
        slotCfg.slotType  = SLOT_BCN;
        slotCfg.byteLen   = 0;
        slotCfg.slotState = SLOT_RX;
        slotCfg.freq      = FHSSgetInitialFreq();
        slotCfg.rateMode  = ExpressLRS_currAirRate_Modparams->rateMode;
        // DBGLN("set tlm denom to %u", ExpressLRS_currTlmDenom);

        ret = Tk86xxSetSlot(0, 1, &slotCfg);
        if (API_SUCCESS != ret) {
            // DBGLN("+ERROR: Set slot error(%d)", ret);
        } else {
            slotCfg.byteLen   = ExpressLRS_currAirRate_Modparams->PayloadLength;
            #if SENSI_TEST
            for (int i = 1; i < SENSI_SLOT_NUM; i++) {
                slotCfg.slotType  = SLOT_DATA;
#if SENSI_TEST_PROFILE
                slotCfg.slotState = (i & 1) ? SLOT_RX : SLOT_TX;
                slotCfg.freq      = FHSSgetFreq((uint8_t)(i - 1));
#else
                slotCfg.slotState = SLOT_RX;
                slotCfg.freq      = FHSSgetInitialFreq();
#endif
                ret = Tk86xxSetSlot(i, 1, &slotCfg);
                if (API_SUCCESS != ret) {
                    // DBGLN("+ERROR: Set slot error(%d)", ret);
                    break;
                }
            }
            #else
            for (int i = 0; i < ELRS_TLM_TEMPLATE_LEN; i++) {
                uint32_t slotIndex = 1 + i;

                uint32_t pos = (i % ELRS_TLM_TEMPLATE_LEN) + 1;
                bool isTlmWindow = (ExpressLRS_currTlmDenom > 1) && ((pos % ExpressLRS_currTlmDenom) == 0);
                SlotState state;
                state = isTlmWindow ? SLOT_TX : SLOT_RX;
                slotCfg.slotType  = SLOT_DATA;
                slotCfg.slotState = state;
                if (InBindingMode) {
                    slotCfg.freq      = FHSSgetInitialFreq();
                } else {
                    slotCfg.freq      = FHSSgetFreq(i);
                }
                ret = Tk86xxSetSlot(slotIndex, 1, &slotCfg);
                if (API_SUCCESS != ret) {
                    DBGLN("+ERROR: Set slot error(%d)", ret);
                    break;
                }
            }
            #endif
        }
    } else {
        // DBGLN("+ERROR: Phy init error(%d)", ret);
    }

    radioInitFlag = (API_SUCCESS == ret) ? true : false;
}

static int start()
{
    if (radioInitFlag) Tk86xxOpenRadio();
#if SENSI_TEST
    queueSensiTestPacket();
#endif
    return DURATION_IMMEDIATELY;
}

static int timeout()
{
    Status        status;
    SignalQuality_t signalQuality = {0};

    if (!Tk86xxCheckStatus(&status)) {
        return DURATION_IMMEDIATELY;
    }

    // DBGLN("slotIrq: %d\r\n", status.slotIrq);
    if (RX_DONE == status.slotIrq) {
        int data_len = Tk86xxRcvData(s_data_buf, sizeof(s_data_buf), &signalQuality);
        bool hasPayload = (data_len > 0) && (data_len <= (int)sizeof(s_data_buf));
        if (hasPayload) {
            applyRssiCompensation(&signalQuality);
        }
        if (rxSlotResultCb) {
            rxSlotResultCb(hasPayload ? &signalQuality : NULL, status.isRxDataSlot != 0U, hasPayload);
        }
    } else if (TX_DONE == status.slotIrq) {
#if SENSI_TEST
        queueSensiTestPacket();
#endif
        if (txDoneCb) txDoneCb();
    } else if ((IDLE_DONE == status.slotIrq) && (status.isRxDataSlot != 0U)) {
        if (rxSlotResultCb) {
            rxSlotResultCb(NULL, true, false);
        }
    } 
    return DURATION_IMMEDIATELY;
}

static int event()
{
    static bool doingBinding = false;

    if (s_pendingAirRateChange) {
        s_pendingAirRateChange = false;
        initialize();
        start();
        return DURATION_IMMEDIATELY;
    }

    if (InBindingMode) {
        initialize();
        start();
        doingBinding = true;
    } else if (doingBinding) {
        doingBinding = false;
        initialize();
        start();
    }

    if (txPowerChanged) {
        txPowerChanged = false;
        Tk86xxTxGainSet(POWERMGNT_getPowerIndBm());
    }

    if (tlmChanged) {
        tlmChanged = false;
        initialize();
        start();
    }
    return DURATION_IMMEDIATELY;
}

device_t ratioRxDevice = {
    .initialize = initialize,
    .start = start,
    .event = event,
    .timeout = timeout
};
