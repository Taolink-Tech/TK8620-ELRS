#ifndef TK86XX_API_H
#define TK86XX_API_H

#include <stddef.h>
#include <stdint.h>

// #define SIM_TUBE

typedef unsigned char      UINT8;
typedef unsigned short int UINT16;
typedef unsigned int       UINT32;

typedef enum {
    API_SUCCESS,
    API_FAILED,
    API_TIMEOUT,
} APIRet;

typedef enum {
    SYNC,
    ASYN,
    ASYN_ANY,
} DeviceMode;

typedef enum {
    IRQ_FIRST_SLOT,
    IRQ_LAST_SLOT,
    IRQ_ALL_SLOT,
} IrqMask;

typedef enum {
    SLOT_BCN,
    SLOT_DATA,
} SlotType;

typedef enum {
    SLOT_TX,
    SLOT_RX,
    SLOT_IDLE,
} SlotState;

typedef enum {
    N40_DBM = -40, // -40dBm
    N39_DBM,       // -39dBm
    N38_DBM,       // -38dBm
    N37_DBM,       // -37dBm
    N36_DBM,       // -36dBm
    N35_DBM,       // -35dBm
    N34_DBM,       // -34dBm
    N33_DBM,       // -33dBm
    N32_DBM,       // -32dBm
    N31_DBM,       // -31dBm
    N30_DBM,       // -30dBm
    N29_DBM,       // -29dBm
    N28_DBM,       // -28dBm
    N27_DBM,       // -27dBm
    N26_DBM,       // -26dBm
    N25_DBM,       // -25dBm
    N24_DBM,       // -24dBm
    N23_DBM,       // -23dBm
    N22_DBM,       // -22dBm
    N21_DBM,       // -21dBm
    N20_DBM,       // -20dBm
    N19_DBM,       // -19dBm
    N18_DBM,       // -18dBm
    N17_DBM,       // -17dBm
    N16_DBM,       // -16dBm
    N15_DBM,       // -15dBm
    N14_DBM,       // -14dBm
    N13_DBM,       // -13dBm
    N12_DBM,       // -12dBm
    N11_DBM,       // -11dBm
    N10_DBM,       // -10dBm
    N9_DBM,        // -9dBm
    N8_DBM,        // -8dBm
    N7_DBM,        // -7dBm
    N6_DBM,        // -6dBm
    N5_DBM,        // -5dBm
    N4_DBM,        // -4dBm
    N3_DBM,        // -3dBm
    N2_DBM,        // -2dBm
    N1_DBM,        // -1dBm
    P0_DBM,        // 0dBm
    P1_DBM,        // 1dBm
    P2_DBM,        // 2dBm
    P3_DBM,        // 3dBm
    P4_DBM,        // 4dBm
    P5_DBM,        // 5dBm
    P6_DBM,        // 6dBm
    P7_DBM,        // 7dBm
    P8_DBM,        // 8dBm
    P9_DBM,        // 9dBm
    P10_DBM,       // 10dBm
    P11_DBM,       // 11dBm
    P12_DBM,       // 12dBm
    P13_DBM,       // 13dBm
    P14_DBM,       // 14dBm
    P15_DBM,       // 15dBm
    P16_DBM,       // 16dBm
    P17_DBM,       // 17dBm
    P18_DBM,       // 18dBm
    P19_DBM,       // 19dBm
    P20_DBM,       // 20dBm
} TxPower;

typedef enum {
    RATE_MODE_4  = 4,
    RATE_MODE_5  = 5,
    RATE_MODE_6  = 6,
    RATE_MODE_7  = 7,
    RATE_MODE_8  = 8,
    RATE_MODE_9  = 9,
    RATE_MODE_10 = 10,
    RATE_MODE_11 = 11,
    RATE_MODE_18 = 18,
    RATE_MODE_24 = 24,
} RateMode;

typedef enum {
    TX_DONE,
    RX_DONE,
    IDLE_DONE,
    WAKEUP_DONE,
    SCAN_CHAN_DONE,
} SlotIrq;

typedef enum {
    WAKEUP_GPIO,
    WAKEUP_TIMER,
    WAKEUP_WIRELESS,
    WAKEUP_GPIO_TIMER,
    WAKEUP_GPIO_WIRELESS,
    WAKEUP_TIMER_WIRELESS,
    WAKEUP_GPIO_TIMER_WIRELESS,
} WakeUpSrc;

typedef enum {
    CLK_RC,
    CLK_OSC32K,
} ClkSrc;

typedef enum {
    SCAN_MODE_0,
    SCAN_MODE_1,
    SCAN_MODE_2,
    SCAN_MODE_3,
    SCAN_MODE_4,
    SCAN_MODE_5,
} ScanMode;

typedef enum {
    SINGLE_TONE,
} TestMode;

typedef enum {
    CASCADE_IN_EN,
    CASCADE_OUT_EN,
    CASCADE_IN_DIS,
    CASCADE_OUT_DIS,
} CascadeSync;

typedef enum {
    SET_FREQ_OFFSET      = 0,
    SET_MAX_POWER_OFFSET = 1,
    SET_ALL_POWER_OFFSET = 2,
    SET_RF_SWITCH        = 3,
    GET_FREQ_OFFSET      = 10,
    GET_MAX_POWER_OFFSET = 11,
    GET_ALL_POWER_OFFSET = 12,
    GET_RF_SWITCH        = 13,
} ICTCtrlMode;

typedef struct
{
    UINT32     slotNum;
    TxPower    rf_pwr;
} InitCfg;

typedef struct
{
    SlotType  slotType;
    SlotState slotState;
    UINT32    freq;
    RateMode  rateMode;
    UINT16    byteLen;
} SlotCfg;

typedef struct
{
    UINT8  chipVer;
    UINT32 chipId;
    UINT32 sdkVer;
    UINT16 flashSize;
    UINT16 remainBufNum;
    UINT32 slotPeriodLen;
} ChipInfo;

typedef struct
{
    SlotIrq slotIrq;
    UINT8 isRxDataSlot;
    UINT8 slotIdx;
    UINT16 slotPeriodCnt;
} Status;

typedef struct
{
    UINT8 pinIdx;
    UINT8 trigLevel;
} GpioSrcCfg;

typedef struct
{
    UINT32 timer;
    ClkSrc clkSrc;
} TimerSrcCfg;

typedef struct
{
    UINT8  mode;
    UINT8  id;
    UINT32 freq;
    UINT32 period;
} WORCfg;

typedef struct
{
    int rssi;
    int snr;
    int cfo;
} SignalQuality_t;

typedef union {
    struct {
        UINT32 freq;
        int    offset;
    } freqOffsetCfg;
    struct {
        int offset;
    } powerOffsetCfg;
    UINT16 powerTable[61][3];
    struct {
        UINT8 pin1Num;
        UINT8 pin2Num;
        UINT8 pin1TxState;
    } rfSwitchCfg;
} ICTCtrlCfg;

typedef enum {
    VERSION_INVALID = 0,
    VERSION_PENDING,
    VERSION_UPDATED
} VerStatus;

typedef struct {
    UINT8 majVer;
    UINT8 minVer;
    UINT8 revVer;
    UINT8 rsvd;
} VerNum;

typedef struct {
    UINT8  verStatus;
    UINT32 verLen;
    VerNum workVerNum;
    VerNum updateVerNum;
    UINT32 crc;
} __attribute__((packed)) VerInfo;

typedef enum
{
    connected,
    tentative,        // RX only
    awaitingModelId,  // TX only
    disconnected,
    MODE_STATES,
    // States below here are special mode states
    noCrossfire,
    bleJoystick,
    NO_CONFIG_SAVE_STATES,
    wifiUpdate,
    serialUpdate,
    // Failure states go below here to display immediately
    FAILURE_STATES,
    radioFailed,
    hardwareUndefined
} connectionState_e;

typedef enum {
    TK86XX_SERIAL_WORD_LENGTH_7B = 7,
    TK86XX_SERIAL_WORD_LENGTH_8B = 8,
} Tk86xxSerialWordLength;

typedef enum {
    TK86XX_SERIAL_STOP_BITS_1 = 0,
    TK86XX_SERIAL_STOP_BITS_2 = 1,
} Tk86xxSerialStopBits;

typedef enum {
    TK86XX_SERIAL_PARITY_NONE = 0,
    TK86XX_SERIAL_PARITY_ODD  = 1,
    TK86XX_SERIAL_PARITY_EVEN = 2,
} Tk86xxSerialParity;

typedef enum {
    TK86XX_SERIAL_DUPLEX_FULL = 0,
    TK86XX_SERIAL_DUPLEX_HALF = 1,
} Tk86xxSerialDuplex;

typedef void (*Tk86xxSerialRxCallback)(UINT8 *data, UINT8 len);

typedef struct {
    UINT32 baudRate;
    UINT32 wordLength;
    UINT32 parity;
    UINT32 stopBits;
    UINT32 duplex;
} Tk86xxSerialConfig;

// Terminal API
APIRet Tk86xxInit(InitCfg *initCfg);
APIRet Tk86xxSetSlot(UINT32 beginSlotIdx, UINT32 slotNum, SlotCfg *slotCfg);
APIRet Tk86xxGetVolt(UINT8 *volt);
APIRet Tk86xxOpenRadio(void);
APIRet Tk86xxCloseRadio(void);
UINT8  Tk86xxCheckStatus(Status *status);
UINT16 Tk86xxRcvData(UINT8 *buf, UINT16 bufLen, SignalQuality_t *signalQuality);
APIRet Tk86xxSendData(UINT8 *data, UINT16 len);
APIRet Tk86xxOtaInitHandshake(SlotState slotState, UINT32 freq, RateMode rateMode, TxPower txPower);
APIRet Tk86xxOtaSetHandshakeState(SlotState slotState, UINT32 freq, RateMode rateMode);
APIRet Tk86xxOtaInitUpgradeSlots(UINT32 freq, RateMode rateMode, TxPower txPower);
APIRet Tk86xxOtaSendData(UINT8 slotIdx, UINT8 *data, UINT16 len);
APIRet Tk86xxICTCtrl(ICTCtrlMode ctrlMode, ICTCtrlCfg *ctrlCfg);
APIRet Tk86xxOtaFlashWrite(UINT32 addr, UINT8 *data, UINT16 len);
APIRet Tk86xxOtaStatusCtrl(VerInfo *versionInfo);
void Tk86xxTxGainSet(int32_t tx_power);
APIRet Tk86xxSerialInit(const Tk86xxSerialConfig *config);
APIRet Tk86xxSerialWrite(const UINT8 *data, UINT32 len);
void Tk86xxSerialRegisterRxCallback(Tk86xxSerialRxCallback callback);
void Tk86xxOnConnectionStateChanged(connectionState_e state);

/* Dummy payload for keepalive frames (length must match actual slot payload size). */
#define TK_DUMMY_PAYLOAD_LEN (13)
static inline UINT8 TkDummyPayloadByte(UINT16 index)
{
    static const UINT8 dummyPayload[TK_DUMMY_PAYLOAD_LEN] = {
        0xA5, 0x5A, 0xA5, 0x5A, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
    };
    return dummyPayload[index];
}

static inline int TkIsDummyPayload(const UINT8 *buf, UINT16 len)
{
    if (len != TK_DUMMY_PAYLOAD_LEN || buf == NULL) {
        return 0;
    }
    for (UINT16 i = 0; i < TK_DUMMY_PAYLOAD_LEN; i++) {
        if (buf[i] != TkDummyPayloadByte(i)) return 0;
    }
    return 1;
}

#endif
