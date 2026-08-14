#include "airport.h"

#if ELRS_HAS_AIRPORT

#define AIRPORT_FIFO_MASK (AIRPORT_FIFO_CAPACITY - 1U)

_Static_assert((AIRPORT_FIFO_CAPACITY & AIRPORT_FIFO_MASK) == 0U,
               "AirPort FIFO capacity must be a power of two");

void AirportFifo_Reset(AirportFifo_t *fifo)
{
    fifo->head = 0U;
    fifo->tail = 0U;
    fifo->overflowCount = 0U;
}

uint16_t AirportFifo_Size(const AirportFifo_t *fifo)
{
    return (uint16_t)(fifo->head - fifo->tail);
}

uint16_t AirportFifo_PushBytes(AirportFifo_t *fifo, const uint8_t *data, uint16_t length)
{
    uint16_t pushed = 0U;

    while (pushed < length) {
        uint16_t head = fifo->head;
        if ((uint16_t)(head - fifo->tail) >= AIRPORT_FIFO_CAPACITY) {
            fifo->overflowCount += (uint32_t)(length - pushed);
            break;
        }

        fifo->data[head & AIRPORT_FIFO_MASK] = data[pushed++];
        __asm__ volatile ("" ::: "memory");
        fifo->head = (uint16_t)(head + 1U);
    }

    return pushed;
}

uint16_t AirportFifo_PopBytes(AirportFifo_t *fifo, uint8_t *data, uint16_t length)
{
    uint16_t popped = 0U;

    while (popped < length) {
        uint16_t tail = fifo->tail;
        if (tail == fifo->head) {
            break;
        }

        data[popped++] = fifo->data[tail & AIRPORT_FIFO_MASK];
        __asm__ volatile ("" ::: "memory");
        fifo->tail = (uint16_t)(tail + 1U);
    }

    return popped;
}

uint8_t AirportPayloadLengthClamp(uint16_t length)
{
    return (uint8_t)(length > AIRPORT_OTA_MAX_PAYLOAD
                         ? AIRPORT_OTA_MAX_PAYLOAD : length);
}

bool AirportPayloadLengthIsValid(uint8_t length)
{
    return length <= AIRPORT_OTA_MAX_PAYLOAD;
}

#endif
