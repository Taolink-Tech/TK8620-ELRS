#pragma once
#include <stdint.h>

#define crclen 256

typedef struct {
    uint8_t crc8tab[crclen];
    uint8_t crcpoly;
} GENERIC_CRC8_s;

typedef struct {
    uint16_t _crctab[crclen];
    uint8_t  _bits;
    uint16_t _bitmask;
    uint16_t _poly;
} Crc2Byte_s;

void GENERIC_CRC8Init(uint8_t poly);
uint8_t GENERIC_CRC8CalcByte(const uint8_t data);
uint8_t GENERIC_CRC8Calc(const uint8_t *data, uint16_t len, uint8_t crc);
void Crc2ByteInit(uint8_t bits, uint16_t poly);
uint16_t Crc2ByteCalc(uint8_t *data, uint8_t len, uint16_t crc);
