#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RX_OTA_SENDER_IDLE = 0,
    RX_OTA_SENDER_HANDSHAKE,
    RX_OTA_SENDER_UPGRADE,
    RX_OTA_SENDER_DONE,
    RX_OTA_SENDER_FAILED,
    RX_OTA_SENDER_VERSION_SAME,
} rx_ota_sender_status_t;

bool RxOtaSender_Start(void);
void RxOtaSender_Update(void);
bool RxOtaSender_IsActive(void);
rx_ota_sender_status_t RxOtaSender_GetStatus(void);
uint8_t RxOtaSender_GetProgressPercent(void);
