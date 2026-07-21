#ifndef __EFUSE_HAL_H__
#define __EFUSE_HAL_H__

#include <stdint.h>

#define EFUSE_TOTAL_SIZE           (32)
#define EFUSE_SN_INFO_START_ADDR   (0)
#define EFUSE_SN_INFO_SIZE         (16)
#define EFUSE_USER_ZONE_START_ADDR (EFUSE_SN_INFO_START_ADDR + EFUSE_SN_INFO_SIZE)
#define EFUSE_USER_ZONE_SIZE       (EFUSE_TOTAL_SIZE - EFUSE_SN_INFO_SIZE)

typedef struct {
    uint32_t chip_type      : 8;
    uint32_t hw_board_num   : 10;
    uint32_t week           : 6;
    uint32_t year           : 7;
    uint32_t reserved1      : 1;
    uint32_t assembly_house : 8;
    uint32_t product_type   : 4;
    uint32_t chip_ser_num   : 20;
    uint32_t chip_id;
} __attribute__((packed)) chip_sn_info_t;


void efuse_init(void);


int efuse_read_sn_info(chip_sn_info_t *sn_info);


int efuse_user_read(uint8_t addr, uint16_t *buf, uint8_t size);


int efuse_user_write(uint8_t addr, uint16_t *buf, uint8_t size);

#endif
