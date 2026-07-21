#include "rx_ota_sender.h"

#include <stdint.h>
#include <string.h>

#include "flash_hal.h"
#include "logging.h"
#include "POWERMGNT.h"
#include "tk86xx_api.h"
#include "tk86xx_platform.h"

#define RX_OTA_XIP __attribute__((section(".xip_text"), noinline, noclone, used))

/* Keep clear of SDK PHY backup sectors at 0x7E000 and 0x7F000. */
#define RX_OTA_META_ADDR              0x7D000U
#define RX_OTA_META_MAGIC             0x41544F52U
#define RX_OTA_META_VERSION           1U
#define RX_OTA_META_SIZE              40U
#define RX_OTA_IMAGE_MAX_LEN          OTA_CODE_LENGTH
#define RX_OTA_PACKET_PAYLOAD_LEN     512U
#define RX_OTA_PACKET_LEN             (RX_OTA_PACKET_PAYLOAD_LEN + 2U)
#define RX_OTA_TERMINAL_ID_LEN        8U
#define RX_OTA_BURN_RESPONSE_LEN      3U
#define RX_OTA_BURN_RESPONSE_RESEND   2U
#define RX_OTA_BURN_RESPONSE_FINISH   3U
#define RX_OTA_UPGRADE_BCN_SLOT       0U
#define RX_OTA_UPGRADE_TX_SLOT        1U
#define RX_OTA_HANDSHAKE_REQ_TIMEOUT  50U
#define RX_OTA_HANDSHAKE_RSP_TIMEOUT_MIN 35U
#define RX_OTA_HANDSHAKE_RSP_TIMEOUT_STEP 5U
#define RX_OTA_HANDSHAKE_ACK_TIMEOUT  1000U
#define RX_OTA_UPGRADE_TIMEOUT        2000U

typedef enum {
    STATE_IDLE = 0,
    STATE_WAIT_HANDSHAKE_TX,
    STATE_WAIT_HANDSHAKE_RESPONSE,
    STATE_WAIT_ACK_TX,
    STATE_WAIT_UPGRADE_BCN,
    STATE_SENDING,
    STATE_WAIT_FINISH,
    STATE_DONE,
    STATE_FAILED,
    STATE_VERSION_SAME,
} RxOtaState;

typedef struct {
    uint32_t magic;
    uint16_t formatVersion;
    uint16_t size;
    uint32_t imageOffset;
    uint32_t firmwareLen;
    uint32_t firmwareCrc;
    uint32_t freq;
    uint8_t version[4];
    uint8_t targetId[RX_OTA_TERMINAL_ID_LEN];
    uint16_t crc16;
    uint16_t reserved;
} __attribute__((packed)) RxOtaMeta;

static RxOtaState s_state = STATE_IDLE;
static RxOtaMeta s_meta;
static uint8_t s_terminalId[RX_OTA_TERMINAL_ID_LEN];
static uint16_t s_nextPacketIndex;
static uint16_t s_totalPackets;
static uint32_t s_deadlineMs;
static uint8_t s_rxBuf[16];
static uint8_t s_handshakeAttempt;
static bool s_handshakeInitialized;
static bool s_firstPacketQueued;

static RX_OTA_XIP bool sendPacket(uint16_t index);
static RX_OTA_XIP void sendNextPacket(void);

static RX_OTA_XIP uint16_t crc16_modbus(const uint8_t *buf, uint32_t len)
{
    uint16_t crc = 0xFFFFU;

    while (len--) {
        crc ^= *buf++;
        for (uint8_t i = 0; i < 8U; i++) {
            if (crc & 1U) {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            } else {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

static RX_OTA_XIP bool metaRead(RxOtaMeta *meta)
{
    if (flash_sys_read(RX_OTA_META_ADDR, (uint8_t *)meta, sizeof(*meta)) < 0) {
        DBGLN("[RXOTA] meta read failed");
        return false;
    }

    if ((meta->magic != RX_OTA_META_MAGIC) ||
        (meta->formatVersion != RX_OTA_META_VERSION) ||
        (meta->size != sizeof(*meta))) {
        DBGLN("[RXOTA] meta invalid at %x magic=%x version=%u size=%u",
              (unsigned)RX_OTA_META_ADDR,
              (unsigned)meta->magic,
              meta->formatVersion,
              meta->size);
        return false;
    }

    const uint16_t expect = crc16_modbus((const uint8_t *)meta, sizeof(*meta) - 4U);
    if (expect != meta->crc16) {
        DBGLN("[RXOTA] meta crc invalid %04x:%04x", meta->crc16, expect);
        return false;
    }

    if ((meta->imageOffset != OTA_CODE_SAVE_START_ADDR) ||
        (meta->firmwareLen == 0U) ||
        (meta->firmwareLen > RX_OTA_IMAGE_MAX_LEN) ||
        ((meta->imageOffset + meta->firmwareLen) > (OTA_CODE_SAVE_START_ADDR + OTA_CODE_LENGTH))) {
        DBGLN("[RXOTA] image range invalid");
        return false;
    }

    return true;
}

static RX_OTA_XIP void setDeadline(uint32_t timeoutMs)
{
    s_deadlineMs = millis() + timeoutMs;
}

static RX_OTA_XIP bool isTimedOut(void)
{
    return ((int32_t)(millis() - s_deadlineMs) >= 0);
}

static RX_OTA_XIP void fail(const char *reason)
{
    DBGLN("[RXOTA] failed: %s", reason);
    Tk86xxCloseRadio();
    s_handshakeInitialized = false;
    s_state = STATE_FAILED;
}

static RX_OTA_XIP bool initHandshakeSlot(SlotState slotState)
{
    return Tk86xxOtaInitHandshake(slotState, s_meta.freq, RATE_MODE_18,
                                  (TxPower)POWERMGNT_getPowerIndBm()) == API_SUCCESS;
}

static RX_OTA_XIP bool setHandshakeSlotState(SlotState slotState)
{
    return Tk86xxOtaSetHandshakeState(slotState, s_meta.freq, RATE_MODE_18) == API_SUCCESS;
}

static RX_OTA_XIP bool sendHandshakeRequest(void)
{
    uint8_t req[RX_OTA_TERMINAL_ID_LEN + 4U];

    memcpy(req, s_meta.targetId, RX_OTA_TERMINAL_ID_LEN);
    memcpy(req + RX_OTA_TERMINAL_ID_LEN, s_meta.version, sizeof(s_meta.version));

    if (!s_handshakeInitialized) {
        if (!initHandshakeSlot(SLOT_TX)) {
            return false;
        }
        s_handshakeInitialized = true;
    } else if (!setHandshakeSlotState(SLOT_TX)) {
        return false;
    }

    if (Tk86xxOtaSendData(1, req, sizeof(req)) != API_SUCCESS) {
        return false;
    }

    s_handshakeAttempt++;
    setDeadline(RX_OTA_HANDSHAKE_REQ_TIMEOUT);
    s_state = STATE_WAIT_HANDSHAKE_TX;
    return true;
}

static RX_OTA_XIP bool waitHandshakeResponse(void)
{
    if (!setHandshakeSlotState(SLOT_RX) || (Tk86xxOpenRadio() != API_SUCCESS)) {
        return false;
    }
    setDeadline(RX_OTA_HANDSHAKE_RSP_TIMEOUT_MIN +
                ((uint32_t)(s_handshakeAttempt & 3U) * RX_OTA_HANDSHAKE_RSP_TIMEOUT_STEP));
    s_state = STATE_WAIT_HANDSHAKE_RESPONSE;
    return true;
}

static RX_OTA_XIP bool sendHandshakeAck(void)
{
    uint8_t ack[RX_OTA_TERMINAL_ID_LEN + 4U + 4U];

    memcpy(ack, s_terminalId, RX_OTA_TERMINAL_ID_LEN);
    memcpy(ack + RX_OTA_TERMINAL_ID_LEN, &s_meta.firmwareLen, sizeof(s_meta.firmwareLen));
    memcpy(ack + RX_OTA_TERMINAL_ID_LEN + 4U, &s_meta.firmwareCrc, sizeof(s_meta.firmwareCrc));

    if (!setHandshakeSlotState(SLOT_TX)) {
        DBGLN("[RXOTA] handshake ack init failed");
        return false;
    }

    if (Tk86xxOtaSendData(1, ack, sizeof(ack)) != API_SUCCESS) {
        DBGLN("[RXOTA] handshake ack send failed");
        return false;
    }

    setDeadline(RX_OTA_HANDSHAKE_ACK_TIMEOUT);
    s_state = STATE_WAIT_ACK_TX;
    return true;
}

static RX_OTA_XIP bool initUpgradeSlots(void)
{
    s_handshakeInitialized = false;
    if (Tk86xxOtaInitUpgradeSlots(s_meta.freq, RATE_MODE_18,
                                  (TxPower)POWERMGNT_getPowerIndBm()) != API_SUCCESS) {
        return false;
    }

    s_nextPacketIndex = 1;
    s_firstPacketQueued = false;
    if (!sendPacket(s_nextPacketIndex)) {
        return false;
    }
    s_nextPacketIndex++;
    s_firstPacketQueued = true;
    s_state = STATE_WAIT_UPGRADE_BCN;
    setDeadline(RX_OTA_UPGRADE_TIMEOUT);
    DBGLN("[RXOTA] upgrade slots ready, packets=%u", s_totalPackets);
    return true;
}

static RX_OTA_XIP bool sendPacket(uint16_t index)
{
    uint8_t txBuf[RX_OTA_PACKET_LEN];
    const uint32_t offset = (uint32_t)(index - 1U) * RX_OTA_PACKET_PAYLOAD_LEN;
    const uint32_t remaining = (offset < s_meta.firmwareLen) ? (s_meta.firmwareLen - offset) : 0U;
    const uint32_t readLen = remaining > RX_OTA_PACKET_PAYLOAD_LEN ? RX_OTA_PACKET_PAYLOAD_LEN : remaining;

    if ((index == 0U) || (index > s_totalPackets) || (readLen == 0U)) {
        return false;
    }

    memset(txBuf, 0xFF, sizeof(txBuf));
    memcpy(txBuf, &index, sizeof(index));
    if (flash_sys_read(s_meta.imageOffset + offset, txBuf + 2U, readLen) < 0) {
        return false;
    }

    if (Tk86xxOtaSendData(1, txBuf, sizeof(txBuf)) != API_SUCCESS) {
        return false;
    }

    DBGLN("[RXOTA] send packet %u/%u", index, s_totalPackets);
    setDeadline(RX_OTA_UPGRADE_TIMEOUT);
    return true;
}

static RX_OTA_XIP void continueAfterPacketSend(void)
{
    if (s_nextPacketIndex > s_totalPackets) {
        s_state = STATE_WAIT_FINISH;
    } else {
        s_state = STATE_SENDING;
    }
}

static RX_OTA_XIP void sendNextPacket(void)
{
    if (s_nextPacketIndex > s_totalPackets) {
        s_state = STATE_WAIT_FINISH;
        setDeadline(RX_OTA_UPGRADE_TIMEOUT);
        return;
    }

    if (!sendPacket(s_nextPacketIndex)) {
        fail("send packet");
        return;
    }
    s_nextPacketIndex++;
    continueAfterPacketSend();
}

static RX_OTA_XIP void handleBurnResponse(void)
{
    SignalQuality_t quality = {0};
    uint16_t len = Tk86xxRcvData(s_rxBuf, sizeof(s_rxBuf), &quality);

    if (len != RX_OTA_BURN_RESPONSE_LEN) {
        return;
    }

    if (s_rxBuf[2] == RX_OTA_BURN_RESPONSE_RESEND) {
        uint16_t packetIndex;
        memcpy(&packetIndex, s_rxBuf, sizeof(packetIndex));
        DBGLN("[RXOTA] resend %u", packetIndex);
        if (!sendPacket(packetIndex)) {
            fail("resend packet");
        } else {
            continueAfterPacketSend();
        }
    } else if (s_rxBuf[2] == RX_OTA_BURN_RESPONSE_FINISH) {
        DBGLN("[RXOTA] finish");
        Tk86xxCloseRadio();
        s_handshakeInitialized = false;
        s_state = STATE_DONE;
    }
}

RX_OTA_XIP bool RxOtaSender_Start(void)
{
    if (RxOtaSender_IsActive()) {
        return true;
    }

    memset(&s_meta, 0, sizeof(s_meta));
    if (!metaRead(&s_meta)) {
        s_state = STATE_FAILED;
        return false;
    }

    s_totalPackets = (uint16_t)((s_meta.firmwareLen + RX_OTA_PACKET_PAYLOAD_LEN - 1U) / RX_OTA_PACKET_PAYLOAD_LEN);
    if (s_totalPackets == 0U) {
        s_state = STATE_FAILED;
        return false;
    }

    s_handshakeAttempt = 0;
    s_handshakeInitialized = false;
    if (!sendHandshakeRequest()) {
        fail("handshake request");
        return false;
    }

    return true;
}

RX_OTA_XIP void RxOtaSender_Update(void)
{
    Status status;

    if ((s_state == STATE_IDLE) || (s_state == STATE_DONE) ||
        (s_state == STATE_FAILED) || (s_state == STATE_VERSION_SAME)) {
        return;
    }

    if (!Tk86xxCheckStatus(&status)) {
        if (isTimedOut()) {
            if ((s_state == STATE_WAIT_HANDSHAKE_TX) || (s_state == STATE_WAIT_HANDSHAKE_RESPONSE)) {
                (void)sendHandshakeRequest();
                return;
            }
            fail("timeout");
        }
        return;
    }

    if (status.slotIrq == TX_DONE) {
        if (s_state == STATE_WAIT_HANDSHAKE_TX) {
            if (!waitHandshakeResponse()) {
                fail("wait response");
            }
        } else if (s_state == STATE_WAIT_ACK_TX) {
            if (!initUpgradeSlots()) {
                fail("upgrade init");
            }
        } else if ((s_state == STATE_SENDING) && (status.slotIdx == RX_OTA_UPGRADE_TX_SLOT)) {
            sendNextPacket();
        }
        return;
    }

    if (status.slotIrq != RX_DONE) {
        if (s_state == STATE_WAIT_HANDSHAKE_RESPONSE) {
            DBGLN("[RXOTA] wait rsp irq=%u slot=%u rx=%u",
                  (unsigned)status.slotIrq,
                  (unsigned)status.slotIdx,
                  (unsigned)status.isRxDataSlot);
        }
        return;
    }

    if (s_state == STATE_WAIT_HANDSHAKE_RESPONSE) {
        SignalQuality_t quality = {0};
        uint16_t len = Tk86xxRcvData(s_rxBuf, sizeof(s_rxBuf), &quality);
        (void)quality;
        if (len != RX_OTA_TERMINAL_ID_LEN) {
            return;
        }

        if (memcmp(s_rxBuf, "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", RX_OTA_TERMINAL_ID_LEN) == 0) {
            DBGLN("[RXOTA] version same");
            Tk86xxCloseRadio();
            s_handshakeInitialized = false;
            s_state = STATE_VERSION_SAME;
            return;
        }

        memcpy(s_terminalId, s_rxBuf, RX_OTA_TERMINAL_ID_LEN);
        if (!sendHandshakeAck()) {
            fail("handshake ack");
        }
    } else if ((s_state == STATE_WAIT_UPGRADE_BCN) || (s_state == STATE_SENDING)) {
        if ((status.isRxDataSlot == 0U) && (status.slotIdx == RX_OTA_UPGRADE_BCN_SLOT)) {
            if (s_state == STATE_WAIT_UPGRADE_BCN) {
                DBGLN("[RXOTA] upgrade bcn sync slot=%u", (unsigned)status.slotIdx);
                s_state = s_firstPacketQueued ? STATE_SENDING : STATE_WAIT_UPGRADE_BCN;
            }
        } else {
            handleBurnResponse();
        }
    } else if (s_state == STATE_WAIT_FINISH) {
        if (status.isRxDataSlot != 0U) {
            handleBurnResponse();
        }
    }
}

RX_OTA_XIP bool RxOtaSender_IsActive(void)
{
    return (s_state != STATE_IDLE) &&
           (s_state != STATE_DONE) &&
           (s_state != STATE_FAILED) &&
           (s_state != STATE_VERSION_SAME);
}

RX_OTA_XIP rx_ota_sender_status_t RxOtaSender_GetStatus(void)
{
    switch (s_state) {
    case STATE_IDLE:
        return RX_OTA_SENDER_IDLE;
    case STATE_DONE:
        return RX_OTA_SENDER_DONE;
    case STATE_FAILED:
        return RX_OTA_SENDER_FAILED;
    case STATE_VERSION_SAME:
        return RX_OTA_SENDER_VERSION_SAME;
    case STATE_WAIT_UPGRADE_BCN:
    case STATE_SENDING:
    case STATE_WAIT_FINISH:
        return RX_OTA_SENDER_UPGRADE;
    default:
        return RX_OTA_SENDER_HANDSHAKE;
    }
}

RX_OTA_XIP uint8_t RxOtaSender_GetProgressPercent(void)
{
    if ((s_state == STATE_DONE) || (s_state == STATE_VERSION_SAME)) {
        return 100U;
    }

    if ((s_state == STATE_IDLE) || (s_state == STATE_FAILED) || (s_totalPackets == 0U)) {
        return 0U;
    }

    if ((s_state == STATE_WAIT_HANDSHAKE_TX) ||
        (s_state == STATE_WAIT_HANDSHAKE_RESPONSE) ||
        (s_state == STATE_WAIT_ACK_TX)) {
        return 0U;
    }

    uint16_t sentPackets = (s_nextPacketIndex > 0U) ? (uint16_t)(s_nextPacketIndex - 1U) : 0U;
    if (sentPackets > s_totalPackets) {
        sentPackets = s_totalPackets;
    }

    return (uint8_t)(((uint32_t)sentPackets * 100U) / s_totalPackets);
}
