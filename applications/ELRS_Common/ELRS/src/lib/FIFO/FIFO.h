/*
 * FIFO Buffer
 * Implementation uses arrays to conserve memory
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Daniel Eisterhold
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 *
 * Modified/Ammended by Alessandro Carcione 2020
 */

#pragma once

// #include "targets.h"
#include <stdint.h>
#include <stdbool.h>
#include "logging.h"
#include "helpers.h"

/**
 * @brief A FIFO which can be made thread/SMP safe using coarse-grained locking via `lock`/`unlock` methods.
 *
 * The FIFO also has helper methods for pushing/popping 16-bit size prefixes to the FIFO. This is useful
 * for FIFOs that are used to hold "packets" of data.
 *
 * @tparam FIFO_SIZE size of the FIFO in bytes
 */
// template <uint32_t FIFO_SIZE>
#define TELEMETRY_FIFO_SIZE 512
#define FIFO_SIZE TELEMETRY_FIFO_SIZE
typedef struct {
    uint8_t buffer[FIFO_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t numElements;
    /**
     * @brief Access an element in the FIFO at the specified index without removing it.
     * The index is calculated relative to the current `head` position.
     *
     * @param index The zero-based index of the element to access within the FIFO.
     * @return The value of the element at the specified index in the FIFO.
     */
    // ICACHE_RAM_ATTR uint8_t operator [](const uint16_t index) const
    // {
    //     return buffer[(head + index) % FIFO_SIZE];
    // }
} FIFO_t;

/**
 * @brief reset the FIFO back to empty
 */
static inline void flush(FIFO_t *fifo)
{
    fifo->head = 0;
    fifo->tail = 0;
    fifo->numElements = 0;
}

/**
 * @brief Push a single byte to the FIFO, FIFO is flushed if this byte will not fit and the byte is not pushed
 *
 * @param data
 */
static inline void push(FIFO_t *fifo, const uint8_t data)
{
    if (fifo->numElements == FIFO_SIZE)
    {
        ERRLN("Buffer full, will flush");
        flush(fifo);
        return;
    } else {
        fifo->numElements++;
        fifo->buffer[fifo->tail] = data;
        fifo->tail = (fifo->tail + 1) % FIFO_SIZE;
    }
}

/**
 * @brief Push all bytes to FIFO, if all the bytes will not fit then the FIFO is flushed and no bytes are pushed
 *
 * @param data pointer to the bytes to be pushed onto the FIFO
 * @param len number of bytes in `data` to push
 */
static inline void pushBytes(FIFO_t *fifo, const uint8_t *data, uint16_t len)
{
    if (fifo->numElements + len > FIFO_SIZE)
    {
        ERRLN("Buffer full, will flush");
        flush(fifo);
        return;
    }
    for (int i = 0; i < len; i++)
    {
        fifo->buffer[fifo->tail] = data[i];
        fifo->tail = (fifo->tail + 1) % FIFO_SIZE;
    }
    fifo->numElements += len;
}

/**
 * @brief Pop a single byte (returns 0 if no bytes left)
 * @return the byte on the head of FIFO
 */
static inline uint8_t pop(FIFO_t *fifo)
{
    if (fifo->numElements == 0)
    {
        return 0;
    }
    fifo->numElements--;
    uint8_t data = fifo->buffer[fifo->head];
    fifo->head = (fifo->head + 1) % FIFO_SIZE;
    return data;
}

/**
 * @brief Pops `len` bytes into the buffer pointed to by `data`.
 * If there are not enough bytes in the FIFO then the FIFO is flushed and the bytes are not read
 *
 * @param data pointer to a buffer where the bytes are popped into
 * @param len number of bytes to pop from teh FIFO
 */
static inline void popBytes(FIFO_t *fifo, uint8_t *data, uint16_t len)
{
    if (fifo->numElements < len)
    {
        flush(fifo);
        return;
    }
    fifo->numElements -= len;
    for (int i = 0; i < len; i++)
    {
        data[i] = fifo->buffer[fifo->head];
        fifo->head = (fifo->head + 1) % FIFO_SIZE;
    }
}

/**
 * @brief return the first byte in the FIFO without removing it from the FIFO
 * Safe to call without locking
 *
 * @return uint8_t the fist byte in the FIFO
 */
static inline uint8_t peek(FIFO_t *fifo)
{
    if (fifo->numElements == 0)
    {
        return 0;
    }
    uint8_t data = fifo->buffer[fifo->head];
    return data;
}

/**
 * @brief return the number of bytes in the FIFO
 * Safe to call without locking
 *
 * @return number of bytes in the FIFO
 */
static inline uint16_t size(FIFO_t *fifo)
{
    return fifo->numElements;
}

/**
 * @brief return the number of bytes free in the FIFO
 * Safe to call without locking
 *
 * @return number of bytes free in the FIFO
 */
static inline uint16_t freeSize(FIFO_t *fifo)
{
    return FIFO_SIZE - fifo->numElements;
}

/**
 * @brief push a 16-bit size prefix onto the FIFO
 *
 * @param size the size prefix to be pushed to the FIFO
 */
static inline void pushSize(FIFO_t *fifo, uint16_t size)
{
    push(fifo, size & 0xFF);
    push(fifo, (size >> 8) & 0xFF);
}

/**
 * @brief return the size prefix from the head of the FIFO, without removing it from the FIFO
 *
 * @param size the size prefix from the head of the FIFO
 */
static inline uint16_t peekSize(FIFO_t *fifo)
{
    if (size(fifo) > 1)
    {
        return (uint16_t)fifo->buffer[fifo->head] + ((uint16_t)fifo->buffer[(fifo->head + 1) % FIFO_SIZE] << 8);
    }
    return 0;
}

/**
 * @brief return the size prefix from the head of the FIFO, also removing it from the FIFO
 *
 * @param size the size prefix from the head of the FIFO
 */
static inline uint16_t popSize(FIFO_t *fifo)
{
    if (size(fifo) > 1)
    {
        return (uint16_t)pop(fifo) + ((uint16_t)pop(fifo) << 8);
    }
    return 0;
}

/**
 * @brief Check to see if the FIFO can accept the number of bytes in the parameter
 *
 * @return true if the FIFO can accept the number of bytes requested
 */
static inline bool available(FIFO_t *fifo, uint16_t requiredSize)
{
    return (fifo->numElements + requiredSize) < FIFO_SIZE;
}

/**
 * @brief  Ensure that there is enough room in the FIFO for the requestedSize in bytes.
 *
 * "packets" are popped from the head of the FIFO until there is enough room available.
 * This method assumes that on the FIFO contains 8-bit length-prefixed data packets.
 *
 * @param requiredSize the number of bytes required to be available
 * @return true if the required amount of bytes will fit in the FIFO
 */
static inline bool ensure(FIFO_t *fifo, uint16_t requiredSize)
{
    if(requiredSize > FIFO_SIZE)
    {
        return false;
    }
    while(!available(fifo, requiredSize))
    {
        uint8_t len = pop(fifo);
        fifo->head = (fifo->head + len) % FIFO_SIZE;
        fifo->numElements -= len;
    }
    return true;
}

/**
 * @brief Sets a value at a specified index in the FIFO buffer.
 * The index is calculated relative to the current `head` position.
 *
 * @param index The zero-based index of the element to access within the FIFO.
 * @param value The value to be stored in the FIFO buffer at the specified index.
 */
static inline void set(FIFO_t *fifo, const uint16_t index, const uint8_t value)
{
    fifo->buffer[(fifo->head + index) % FIFO_SIZE] = value;
}

static inline uint8_t get(FIFO_t *fifo, const uint16_t index)
{
    return fifo->buffer[(fifo->head + index) % FIFO_SIZE];
}

/**
 * @brief Skip a specified number of elements in the FIFO, adjusting the head index.
 * This method reduces the number of elements in the FIFO and moves the head
 * pointer forward by the given length, wrapping around if necessary.
 *
 * @param len The number of elements to skip. The actual number skipped will not exceed
 * the current number of elements in the FIFO.
 */
static inline void skip(FIFO_t *fifo, const uint16_t len)
{
    fifo->numElements -= MIN((uint32_t)len, fifo->numElements);
    fifo->head = (fifo->head + len) % FIFO_SIZE;
}
