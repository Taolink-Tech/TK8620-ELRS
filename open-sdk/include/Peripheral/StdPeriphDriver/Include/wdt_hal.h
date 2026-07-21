

#ifndef __WDT_HAL_H__
#define __WDT_HAL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    COUNT_MODE = 0, 
    RESET_MODE = 1, 
} wdt_mode_e;

typedef struct {
    uint32_t   count; 
    wdt_mode_e mode;
} wdt_init_t;

typedef void (*wdt_callback_t)(void);


void wdt_init(wdt_init_t *param);


void wdt_deinit(void);


void wdt_enable(void);


void wdt_disable(void);


void wdt_feed(void);


void wdt_callback_register(wdt_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif
