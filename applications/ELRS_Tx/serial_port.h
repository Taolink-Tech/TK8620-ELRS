#ifndef __SERIAL_PORT_H__
#define __SERIAL_PORT_H__

#include <stdint.h>

typedef enum {
    CRSF_SERIAL_BAUD_400K = 0,
    CRSF_SERIAL_BAUD_420K = 1,
    CRSF_SERIAL_BAUD_921K = 2,
    CRSF_SERIAL_BAUD_MAX,
} crsf_serial_baud_e;

static inline uint32_t crsfSerialPortBaudEnumToBaud(crsf_serial_baud_e baudEnum)
{
    switch (baudEnum) {
        case CRSF_SERIAL_BAUD_400K: return 400000U;
        case CRSF_SERIAL_BAUD_420K: return 420000U;
        case CRSF_SERIAL_BAUD_921K: return 921600U;
        default: return 400000U;
    }
}

#endif
