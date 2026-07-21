#include "elrs_eeprom.h"
#include "flash_hal.h"

static uint8_t ELRS_EEPROM_ReadByte(const uint32_t address)
{
    (void)address;
    return 0;
}

static void ELRS_EEPROM_WriteByte(const uint32_t address, const uint8_t value)
{
    (void)address;
    (void)value;
}

static void ELRS_EEPROM_Commit(uint16_t addrOffset, uint8_t *buf, uint32_t bytes)
{
    flash_user_erase(addrOffset, bytes);
    flash_user_write(addrOffset, buf, bytes);
}

static void ELRS_EEPROM_Get(uint16_t addrOffset,uint8_t *buf, uint32_t bytes)
{
    flash_user_read(addrOffset, buf, bytes);
}

static void ELRS_EEPROM_Put(uint32_t addr, const void *value)
{
}

void ELRS_EEPROM_Init(ELRS_EEPROM_t *eeprom)
{
    eeprom->ReadByte = ELRS_EEPROM_ReadByte;
    eeprom->WriteByte = ELRS_EEPROM_WriteByte;
    eeprom->Commit = ELRS_EEPROM_Commit;
    eeprom->Get = ELRS_EEPROM_Get;
    eeprom->Put = ELRS_EEPROM_Put;
}
