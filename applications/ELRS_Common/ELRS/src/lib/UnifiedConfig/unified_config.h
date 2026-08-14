#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifndef ELRS_UNIFIED
#define ELRS_UNIFIED 0
#endif

#ifndef ELRS_AIRPORT
#define ELRS_AIRPORT 0
#endif

#define ELRS_HAS_AIRPORT ((ELRS_AIRPORT) || (ELRS_UNIFIED))

typedef enum {
    UNIFIED_ROLE_TX = 1,
    UNIFIED_ROLE_RX = 2,
} unified_role_e;

typedef enum {
    UNIFIED_MODE_RC = 0,
    UNIFIED_MODE_AIRPORT = 1,
} unified_mode_e;

typedef void (*UnifiedConfigForwardByteFn)(uint8_t byte);
typedef void (*UnifiedConfigLifecycleFn)(void);

#if ELRS_UNIFIED

void UnifiedConfig_Init(unified_role_e role,
                        const char *version,
                        const char *buildId,
                        UnifiedConfigLifecycleFn stopNormalOperation,
                        UnifiedConfigLifecycleFn requestReboot);
bool UnifiedConfig_IsAirport(void);
bool UnifiedConfig_IsSessionActive(void);
bool UnifiedConfig_IsRecordValid(void);
unified_mode_e UnifiedConfig_GetStoredMode(void);
void UnifiedConfig_SetLoggingEnabled(bool enabled);
bool UnifiedConfig_IsLoggingEnabled(void);
void UnifiedConfig_FilterBytes(const uint8_t *data, uint16_t length,
                               UnifiedConfigForwardByteFn forwardByte);
void UnifiedConfig_Update(uint32_t nowMs);
void UnifiedConfig_StartBootProbe(void);
void UnifiedConfig_EndBootProbe(void);

#else

static inline void UnifiedConfig_Init(unified_role_e role,
                                      const char *version,
                                      const char *buildId,
                                      UnifiedConfigLifecycleFn stopNormalOperation,
                                      UnifiedConfigLifecycleFn requestReboot)
{
    (void)role;
    (void)version;
    (void)buildId;
    (void)stopNormalOperation;
    (void)requestReboot;
}

static inline bool UnifiedConfig_IsAirport(void)
{
    return ELRS_AIRPORT != 0;
}

static inline bool UnifiedConfig_IsSessionActive(void) { return false; }
static inline bool UnifiedConfig_IsRecordValid(void) { return false; }
static inline unified_mode_e UnifiedConfig_GetStoredMode(void) { return UNIFIED_MODE_RC; }
static inline void UnifiedConfig_SetLoggingEnabled(bool enabled) { (void)enabled; }
static inline bool UnifiedConfig_IsLoggingEnabled(void) { return ELRS_AIRPORT == 0; }
static inline void UnifiedConfig_Update(uint32_t nowMs) { (void)nowMs; }
static inline void UnifiedConfig_StartBootProbe(void) {}
static inline void UnifiedConfig_EndBootProbe(void) {}

#endif
