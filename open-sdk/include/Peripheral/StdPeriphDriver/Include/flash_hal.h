#ifndef __QSPI_FLASH_H
#define __QSPI_FLASH_H
#include <string.h>  // string
#include <stddef.h>  // standard
#include <stdint.h>  // integer
#include <stdbool.h> // boolean

enum qspi_baudrate_div_e {
    QSPI_BAUDRATE_DIV_2,
    QSPI_BAUDRATE_DIV_4,
    QSPI_BAUDRATE_DIV_6,
    QSPI_BAUDRATE_DIV_8,
    QSPI_BAUDRATE_DIV_10,
    QSPI_BAUDRATE_DIV_12,
    QSPI_BAUDRATE_DIV_14,
    QSPI_BAUDRATE_DIV_16,
    QSPI_BAUDRATE_DIV_18,
    QSPI_BAUDRATE_DIV_20,
    QSPI_BAUDRATE_DIV_22,
    QSPI_BAUDRATE_DIV_24,
    QSPI_BAUDRATE_DIV_26,
    QSPI_BAUDRATE_DIV_28,
    QSPI_BAUDRATE_DIV_30,
    QSPI_BAUDRATE_DIV_32,
    QSPI_BAUDRATE_MAX
};
// extern volatile struct qspi_regs_s *const qspi;

#define USER_FLASH_CHECK_ADDR(addr, len)                       \
    do {                                                       \
        if ((addr + len) >= (CUSTOMER_ZONE_LENGTH - 0x1000)) { \
            return -1;                                         \
        }                                                      \
    } while (0)

#define OTA_CODE_SAVE_START_ADDR (0X41000) 
#define OTA_CODE_LENGTH          (0X22800) 
#define OTA_PARAM_START_ADDR     (0X40000) 
#define OTA_PARAM_LENGTH         (0X1000)  
#define CUSTOMER_ZONE_START_ADDR (0X69000) 
#define CUSTOMER_ZONE_LENGTH     (0X17000) 

/********************************************************************
 * SPI Flash interface functions                                   *
 ********************************************************************/
void qspi_bps_divisor_set(enum qspi_baudrate_div_e bps_div);
int  flash_read_uid(char *id, int len);
void flash_sector_erase(uint32_t sector_addr);
int  flash_sys_read(uint32_t addr, uint8_t *buffer, uint32_t len);
int  flash_sys_write(uint32_t addr, uint8_t *buffer, uint32_t len);
int  flash_user_erase(uint32_t addr, uint32_t len);
int  flash_user_write(uint32_t addr, uint8_t *buf, uint32_t len);
int  flash_user_read(uint32_t addr, uint8_t *buf, uint32_t len);
void flash_ota_code_save_zone_erase(void);
int  flash_ota_param_read(uint8_t *buf, uint32_t len);
int  flash_ota_code_save_zone_write(uint32_t addr, uint8_t *buf, uint32_t len);

int  flash_ota_code_save_zone_crc(uint32_t crc, uint32_t length);
void flash_ota_param_zone_write(uint8_t *buf, uint32_t len);
int  flash_ota_code_save_zone_read(uint32_t addr, uint8_t *buf, uint32_t len);

void flash_license_code_read(uint8_t *license_code);
void flash_license_code_write(uint8_t *license_code);

#endif
