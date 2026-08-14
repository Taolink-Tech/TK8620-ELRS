#include <string.h>
#include "SerialIO.h"
#include "SerialCRSF.h"
#include "SerialSBUS.h"
#include "helpers.h"
#include "logging.h"
#include "tk86xx_api.h"
#include "unified_config.h"

extern SerialIO_t serialIO;
static volatile uint16_t s_rx_wr = 0;
static volatile uint16_t s_rx_rd = 0;
static uint8_t sRxDataBuf[defaultMaxSerialWriteSize];

static void SerialIO_BufferByte(uint8_t byte)
{
    uint16_t next = (uint16_t)((s_rx_wr + 1) % defaultMaxSerialWriteSize);
    if (next == s_rx_rd) {
        s_rx_rd = (uint16_t)((s_rx_rd + 1) % defaultMaxSerialWriteSize);
    }
    sRxDataBuf[s_rx_wr] = byte;
    s_rx_wr = next;
}

/* UART RX callback: cache bytes into the ring buffer only. */
RAMCODE_SECTION void SerialIO_UartRxCallback(uint8_t *data, uint8_t len)
{
#if ELRS_UNIFIED
    UnifiedConfig_FilterBytes(data, len, SerialIO_BufferByte);
#else
    for (uint8_t i = 0; i < len; i++) {
        SerialIO_BufferByte(data[i]);
    }
#endif
}

static int SerialIO_getMaxSerialWriteSize(void)
{
    return defaultMaxSerialWriteSize;
}

static int SerialIO_getMaxSerialReadSize(void)
{
    return defaultMaxSerialReadSize;
}

static uint16_t readBytes(uint8_t *dest, uint16_t maxlen)
{
    uint16_t rd;
    uint16_t wr;
    uint16_t available;
    uint16_t toCopy;
    uint16_t firstChunk;

    if (maxlen == 0 || dest == NULL)
        return 0;

    rd = (uint16_t)s_rx_rd;
    wr = (uint16_t)s_rx_wr;
    if (wr >= rd) {
        available = (uint16_t)(wr - rd);
    } else {
        available = (uint16_t)(defaultMaxSerialWriteSize - rd + wr);
    }

    toCopy = (maxlen < available) ? maxlen : available;
    if (toCopy == 0) {
        return 0;
    }

    firstChunk = (toCopy <= (uint16_t)(defaultMaxSerialWriteSize - rd)) ? toCopy : (uint16_t)(defaultMaxSerialWriteSize - rd);
    if (firstChunk > 0) {
        memcpy(dest, &sRxDataBuf[rd], firstChunk);
    }
    if (toCopy > firstChunk) {
        memcpy(dest + firstChunk, sRxDataBuf, toCopy - firstChunk);
    }

    s_rx_rd = (uint16_t)((rd + toCopy) % defaultMaxSerialWriteSize);

    return toCopy;
}

static void SerialIO_processSerialInput(void)
{
    uint8_t buffer[defaultMaxSerialReadSize];
    uint16_t processed = 0;

    /* Drain one ring-buffer capacity per loop without allowing continuous
       UART input to starve the rest of the receiver main loop. */
    while (processed < defaultMaxSerialWriteSize) {
        uint16_t remaining = (uint16_t)(defaultMaxSerialWriteSize - processed);
        uint16_t request = MIN((uint16_t)defaultMaxSerialReadSize, remaining);
        uint16_t size = readBytes(buffer, request);
        if (size == 0) {
            break;
        }
        serialIO.processBytes(buffer, size);
        processed = (uint16_t)(processed + size);
    }
}

static void SerialIO_sendQueuedData(uint32_t maxBytesToSend)
{
    uint32_t bytesWritten = 0;

    while (size(&serialIO._fifo) > peek(&serialIO._fifo) && (bytesWritten + peek(&serialIO._fifo)) < maxBytesToSend)
    {
        uint8_t outPktLen = pop(&serialIO._fifo);
        uint8_t outData[defaultMaxSerialWriteSize];
        popBytes(&serialIO._fifo, outData, outPktLen);
        Tk86xxSerialWrite(outData, outPktLen);
        bytesWritten += outPktLen;
    }
}

void SerialIO_Init(SerialIO_t *const serialIO)
{
    serialIO->processSerialInput = SerialIO_processSerialInput;
    serialIO->sendQueuedData = SerialIO_sendQueuedData;
    serialIO->getMaxSerialWriteSize = SerialIO_getMaxSerialWriteSize;
    serialIO->getMaxSerialReadSize = SerialIO_getMaxSerialReadSize;
    serialIO->sendRCFrame = SerialCRSF_SendRCFrame;
    serialIO->processBytes = SerialCRSF_processBytes;
    serialIO->queueMSPFrameTransmission = SerialCRSF_queueMSPFrameTransmission;
}

void SerialIO_SetProtocol(SerialIO_t *serialIO, eSerialProtocol_e protocol)
{
    if (serialIO == NULL)
        return;

    switch (protocol)
    {
    case PROTOCOL_SBUS:
    case PROTOCOL_INVERTED_SBUS:
        serialIO->sendRCFrame = SerialSBUS_SendRCFrame;
        serialIO->processBytes = SerialSBUS_processBytes;
        serialIO->queueMSPFrameTransmission = SerialSBUS_queueMSPFrameTransmission;
        serialIO->sendQueuedData = SerialIO_sendQueuedData;
        break;

    case PROTOCOL_CRSF:
    case PROTOCOL_INVERTED_CRSF:
    default:
        serialIO->sendRCFrame = SerialCRSF_SendRCFrame;
        serialIO->processBytes = SerialCRSF_processBytes;
        serialIO->queueMSPFrameTransmission = SerialCRSF_queueMSPFrameTransmission;
        serialIO->sendQueuedData = SerialIO_sendQueuedData;
        break;
    }
}
