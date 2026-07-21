#pragma once

#include <stdint.h>

#define RESERVED_EEPROM_SIZE 1024

typedef struct {
    uint8_t (*ReadByte)(const uint32_t address);
    void (*WriteByte)(const uint32_t address, const uint8_t value);
    void (*Commit)(uint16_t addrOffset, uint8_t *buf, uint32_t bytes);
    void (*Get)(uint16_t addrOffset, uint8_t *buf, uint32_t bytes);
    void (*Put)(uint32_t addr, const void *value);
} ELRS_EEPROM_t;

void ELRS_EEPROM_Init(ELRS_EEPROM_t *eeprom);
