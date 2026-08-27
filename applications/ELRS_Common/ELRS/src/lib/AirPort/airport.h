#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef ELRS_AIRPORT
#define ELRS_AIRPORT 0
#endif
#ifndef ELRS_UNIFIED
#define ELRS_UNIFIED 0
#endif

#define ELRS_HAS_AIRPORT ((ELRS_AIRPORT) || (ELRS_UNIFIED))

#define AIRPORT_UART_BAUD_DEFAULT 460800U

#ifndef AIRPORT_UART_BAUD
#define AIRPORT_UART_BAUD AIRPORT_UART_BAUD_DEFAULT
#endif
#ifndef AIRPORT_RF_RATE
#define AIRPORT_RF_RATE RATE_TMS_250HZ
#endif

#define AIRPORT_FIFO_CAPACITY 128U
#define AIRPORT_OTA_MAX_PAYLOAD 10U
#define AIRPORT_CRC_DOMAIN 0x4150U

static inline bool AirportBaudIsSupported(uint32_t baud)
{
    switch (baud) {
    case 4800U:
    case 9600U:
    case 19200U:
    case 38400U:
    case 57600U:
    case 115200U:
    case 230400U:
    case 460800U:
    case 921600U:
        return true;
    default:
        return false;
    }
}

typedef struct {
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint32_t overflowCount;
    uint8_t data[AIRPORT_FIFO_CAPACITY];
} AirportFifo_t;

void AirportFifo_Reset(AirportFifo_t *fifo);
uint16_t AirportFifo_Size(const AirportFifo_t *fifo);
uint16_t AirportFifo_PushBytes(AirportFifo_t *fifo, const uint8_t *data, uint16_t length);
uint16_t AirportFifo_PopBytes(AirportFifo_t *fifo, uint8_t *data, uint16_t length);
uint8_t AirportPayloadLengthClamp(uint16_t length);
bool AirportPayloadLengthIsValid(uint8_t length);
