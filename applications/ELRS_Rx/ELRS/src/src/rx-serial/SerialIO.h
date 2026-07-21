#pragma once

#include <stdbool.h>
#include "device.h"
#include "FIFO.h"
#include "common.h"

#define defaultMaxSerialReadSize 64
#define defaultMaxSerialWriteSize 128

/**
 * Receiver-side serial protocol interface.
 *
 * Each concrete protocol implementation fills in this table so the RX loop can
 * process serial input, queue outgoing data, and emit channel frames through a
 * shared call path.
 */
typedef struct {
    /** Flush any previously queued protocol bytes to the serial output. */
    void (*sendQueuedData)(uint32_t maxBytesToSend);

    /** Read bytes from the serial port and pass them into processBytes(). */
    void (*processSerialInput)(void);

    /**
     * Get the maximum number of bytes to write to the serial port in each call.
     *
     * @return maximum number of bytes to write
     */
    int (*getMaxSerialWriteSize)(void);

    /** Flag indicating that the receiver is in failsafe. */
    bool failsafe;

    /** FIFO used for ancillary serial traffic such as telemetry or MSP. */
    FIFO_t _fifo;

    /**
     * Get the maximum number of bytes to read from the serial port per call.
     *
     * @return the maximum number of bytes to read
     */
    int (*getMaxSerialReadSize)(void);

    /** Protocol-specific byte parser invoked by processSerialInput(). */
    void (*processBytes)(uint8_t *bytes, uint16_t size);

    uint32_t (*sendRCFrame)(bool frameAvailable, bool frameMissed, uint32_t *channelData);
    void (*queueMSPFrameTransmission)(uint8_t *data);
} SerialIO_t;

void SerialIO_Init(SerialIO_t *serialIO);
void SerialIO_SetProtocol(SerialIO_t *serialIO, eSerialProtocol_e protocol);
void SerialIO_UartRxCallback(uint8_t *data, uint8_t len);
