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

#ifndef AIRPORT_UART_BAUD
#define AIRPORT_UART_BAUD 9600U
#endif
#ifndef AIRPORT_RF_RATE
#define AIRPORT_RF_RATE RATE_TMS_250HZ
#endif

#define AIRPORT_FIFO_CAPACITY 128U
#define AIRPORT_OTA_MAX_PAYLOAD 10U
#define AIRPORT_CRC_DOMAIN 0x4150U

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
