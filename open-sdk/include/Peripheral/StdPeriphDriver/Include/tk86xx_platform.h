#ifndef TK86XX_PLATFORM_H
#define TK86XX_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "gpio_hal.h"

typedef enum {
    DISABLE = 0,
    ENABLE  = !DISABLE
} fun_state_e;

#define MAX_PROBES               10
#define MAX_FUNCTION_NAME_LENGTH 32
#define MAX_NESTING_LEVEL        10

typedef struct {
    char     function_name[MAX_FUNCTION_NAME_LENGTH];
    uint32_t calls;
    uint64_t total_time;
    uint32_t min_time;
    uint32_t max_time;
    uint32_t nesting_level;
} ProbeResult;

typedef struct {
    uint32_t start_time;
    uint32_t wait_duration;
    bool     is_waiting;
} WaitTimer;

void delay_us(uint32_t nus);
void delay_ms(uint32_t nms);

uint32_t millis(void);
uint32_t micros(void);

void     timer_meas_start(void);
uint32_t timer_meas_end(void);

void ioout_set(gpio_pin_e io_idx, uint8_t level);
void gpio_keep_set(fun_state_e state);

#ifdef __cplusplus
}
#endif

#endif
