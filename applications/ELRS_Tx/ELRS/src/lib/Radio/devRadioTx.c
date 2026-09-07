#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "devRadioTx.h"
#include "common.h"
#include "helpers.h"
#include "FHSS.h"
#include "crc.h"
#include "stubborn_sender.h"
#include "logging.h"
#include "msptypes.h"
#include "POWERMGNT.h"
#include "CRSFHandset.h"
#include "tk86xx_api.h"
#include "tk86xx_platform.h"

extern StubbornSender_t MspSender;
#define SET_SLOT_MAX         (25)
#define TK8620_TX_POWER_OFFSET 3

#ifndef RSSI_COMP_DB
#define RSSI_COMP_DB (-17)
#endif

static bool radioInitFlag = false;

static void (*rxDoneCb)(uint8_t *data, uint16_t data_len, SignalQuality_t *signalQuality) = NULL;
static void (*txDoneCb)(void) = NULL;
static void (*txAbortCb)(void) = NULL;
static void (*tlmWindowDoneCb)(void) = NULL;

static volatile bool s_pendingAirRateChange = false;
static uint32_t      s_pendingAirRateRestartAtMs = 0;

#if SENSI_TEST
static void queueSensiTestPacket(void)
{
    static uint8_t seq = 0;
    uint8_t payload[BBU_TRX_MAX];
    uint16_t len = ExpressLRS_currAirRate_Modparams ? ExpressLRS_currAirRate_Modparams->PayloadLength : TK_DUMMY_PAYLOAD_LEN;

    if (len > sizeof(payload)) {
        len = sizeof(payload);
    }

    payload[0] = 0x5C;
    payload[1] = seq++;
    for (uint16_t i = 2; i < len; i++) {
        payload[i] = (uint8_t)(0x30U + i + seq);
    }

    Tk86xxSendData(payload, len);
}
#endif

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

static void applyTxPowerOffset(void)
{
    ICTCtrlCfg ctrlCfg = {0};

    if ((Tk86xxICTCtrl(GET_MAX_POWER_OFFSET, &ctrlCfg) == API_SUCCESS) &&
        (ctrlCfg.powerOffsetCfg.offset == TK8620_TX_POWER_OFFSET)) {
        return;
    }

    ctrlCfg.powerOffsetCfg.offset = TK8620_TX_POWER_OFFSET;
    if (Tk86xxICTCtrl(SET_MAX_POWER_OFFSET, &ctrlCfg) == API_SUCCESS) {
        Tk86xxTxGainSet(POWERMGNT_getPowerIndBm());
    }
}

void DevRadioTx_RegisterRxDoneCb(void (*cb)(uint8_t *data, uint16_t data_len, SignalQuality_t *signalQuality))
{
    rxDoneCb = cb;
}

void DevRadioTx_RegisterTxDoneCb(void (*cb)(void))
{
    txDoneCb = cb;
}

void DevRadioTx_RegisterTxAbortCb(void (*cb)(void))
{
    txAbortCb = cb;
}

void DevRadioTx_RegisterTlmWindowDoneCb(void (*cb)(void))
{
    tlmWindowDoneCb = cb;
}

void DevRadioTx_Stop(void)
{
    Tk86xxCloseRadio();
}

void DevRadioTx_RequestTlmRatioChange(uint8_t previousTlmDenom)
{
    const uint32_t intervalUs = ExpressLRS_currAirRate_Modparams ?
        ExpressLRS_currAirRate_Modparams->interval : 4000u;
    const uint32_t slotPeriodMs = (intervalUs + 999u) / 1000u;
    const uint32_t delayMs = MAX(200u, slotPeriodMs * (uint32_t)(previousTlmDenom + 1u));

    s_pendingAirRateRestartAtMs = millis() + delayMs;
    s_pendingAirRateChange = true;
    DBGLN("[TLMRATIO][TX] delayed slot restart old_denom=%u delay_ms=%u",
          (unsigned)previousTlmDenom, (unsigned)delayMs);
}

static void initialize()
{
    APIRet ret = API_FAILED;

    // Any full initialization applies the current telemetry slot layout, so a
    // delayed ratio-only restart is redundant.
    s_pendingAirRateChange = false;

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
    // Tk86xxInit() already stops an active PHY before reinitializing it. Avoid
    // a full RF power cycle during rate changes, which can occasionally leave
    // the transmitter silent until another reinitialization.
    if (txAbortCb) txAbortCb();

    if ((ret = Tk86xxInit(&initCfg)) == API_SUCCESS) {
        applyTxPowerOffset();

        SlotCfg slotCfg = {0};

        slotCfg.slotType  = SLOT_BCN;
        slotCfg.byteLen   = 0;
        slotCfg.slotState = SLOT_TX;
        slotCfg.freq      = FHSSgetInitialFreq();
        slotCfg.rateMode  = ExpressLRS_currAirRate_Modparams->rateMode;
        // DBGLN("set pwr to %d dBm", POWERMGNT_getPowerIndBm());
        // DBGLN("set tlm denom to %u", ExpressLRS_currTlmDenom);
        // DBGLN("set air rate to %u", ExpressLRS_currAirRate_Modparams->rateMode);

        ret = Tk86xxSetSlot(0, 1, &slotCfg);
        if (API_SUCCESS != ret) {
            // DBGLN("+ERROR: Set slot error(%d)", ret);
        } else {
            slotCfg.byteLen   = ExpressLRS_currAirRate_Modparams->PayloadLength;
            #if SENSI_TEST
            for (int i = 1; i < SENSI_SLOT_NUM; i++) {
                slotCfg.slotType  = SLOT_DATA;
#if SENSI_TEST_PROFILE
                slotCfg.slotState = (i & 1) ? SLOT_TX : SLOT_RX;
                slotCfg.freq      = FHSSgetFreq((uint8_t)(i - 1));
#else
                slotCfg.slotState = SLOT_TX;
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
                state = isTlmWindow ? SLOT_RX : SLOT_TX;

                slotCfg.slotType  = SLOT_DATA;
                slotCfg.slotState = state;
                if (InBindingMode) {
                    slotCfg.freq      = FHSSgetInitialFreq();
                } else {
                    slotCfg.freq      = FHSSgetFreq(i);
                }

                ret = Tk86xxSetSlot(slotIndex, 1, &slotCfg);
                if (API_SUCCESS != ret) {
                    // DBGLN("+ERROR: Set slot error(%d)", ret);
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
    CRSFHandset_UartInBufRst();
    return DURATION_IMMEDIATELY;
}

static int timeout()
{
    Status        status;
    SignalQuality_t signalQuality = {0};

    if (s_pendingAirRateChange && (int32_t)(millis() - s_pendingAirRateRestartAtMs) >= 0) {
        txLostSignal = true;
        s_pendingAirRateChange = false;
        devicesTriggerEvent();
    }

    if (!Tk86xxCheckStatus(&status)) {
        return DURATION_IMMEDIATELY;
    }

    if (RX_DONE == status.slotIrq) {
        int data_len = Tk86xxRcvData(s_data_buf, sizeof(s_data_buf), &signalQuality);
        // for (int i = 0; i < data_len; i++) {
        //     DBG("%02X ", s_data_buf[i]);
        // }
        if (data_len > 0) {
            applyRssiCompensation(&signalQuality);
        }
        if (rxDoneCb) rxDoneCb(s_data_buf, data_len, &signalQuality);
    } else if (TX_DONE == status.slotIrq) {
        if (InBindingMode) {
            BindingSendCount++;
            MspSender.senderState = SENDING;
        }
#if SENSI_TEST
        queueSensiTestPacket();
#endif
        if (txDoneCb) txDoneCb();
    }
#if SENSI_TEST
    else if ((IDLE_DONE == status.slotIrq) && (status.isRxDataSlot != 0U)) {
        if (rxDoneCb) rxDoneCb(NULL, 0, NULL);
    }
#endif

    if (status.isRxDataSlot && tlmWindowDoneCb) {
        tlmWindowDoneCb();
    }

    return DURATION_IMMEDIATELY;
}

static int event()
{
    static bool doingBinding = false;

    if (InBindingMode) {
        initialize();
        start();
        doingBinding = true;
        txPowerChanged = false;
        tlmChanged = false;
        txLostSignal = false;
    } else if (doingBinding) {
        doingBinding = false;
        initialize();
        start();
        txPowerChanged = false;
        tlmChanged = false;
        txLostSignal = false;
    } else if (tlmChanged || txLostSignal) {
        // A full reinitialization applies the current power too. Consume all
        // restart-related flags together so event coalescing cannot strand a
        // rate change behind a simultaneous dynamic-power update.
        txPowerChanged = false;
        tlmChanged = false;
        txLostSignal = false;
        initialize();
        start();
    } else if (txPowerChanged) {
        txPowerChanged = false;
        Tk86xxTxGainSet(POWERMGNT_getPowerIndBm());
    }

    return DURATION_IMMEDIATELY;
}

device_t ratioTxDevice = {
    .initialize = initialize,
    .start = start,
    .event = event,
    .timeout = timeout
};
