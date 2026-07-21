#include "crc.h"

static GENERIC_CRC8_s genericCrc8;
static Crc2Byte_s crc2Byte;

void GENERIC_CRC8Init(uint8_t poly)
{
    uint8_t crc;

    for (uint16_t i = 0; i < crclen; i++)
    {
        crc = i;
        for (uint8_t j = 0; j < 8; j++)
        {
            crc = (crc << 1) ^ ((crc & 0x80) ? poly : 0);
        }
        genericCrc8.crc8tab[i] = crc & 0xFF;
    }
}

uint8_t GENERIC_CRC8CalcByte(const uint8_t data)
{
    return genericCrc8.crc8tab[data];
}

uint8_t GENERIC_CRC8Calc(const uint8_t *data, uint16_t len, uint8_t crc)
{
    while (len--)
    {
        crc = genericCrc8.crc8tab[crc ^ *data++];
    }
    return crc;
}

void Crc2ByteInit(uint8_t bits, uint16_t poly)
{
    if (bits == crc2Byte._bits && poly == crc2Byte._poly)
        return;
    crc2Byte._poly = poly;
    crc2Byte._bits = bits;
    crc2Byte._bitmask = (1 << crc2Byte._bits) - 1;
    uint16_t highbit = 1 << (crc2Byte._bits - 1);
    uint16_t crc;
    for (uint16_t i = 0; i < crclen; i++)
    {
        crc = i << (bits - 8);
        for (uint8_t j = 0; j < 8; j++)
        {
            crc = (crc << 1) ^ ((crc & highbit) ? poly : 0);
        }
        crc2Byte._crctab[i] = crc;
    }
}

uint16_t Crc2ByteCalc(uint8_t *data, uint8_t len, uint16_t crc)
{
    while (len--)
    {
        crc = (crc << 8) ^ crc2Byte._crctab[((crc >> (crc2Byte._bits - 8)) ^ (uint16_t) *data++) & 0x00FF];
    }
    return crc & crc2Byte._bitmask;
}
