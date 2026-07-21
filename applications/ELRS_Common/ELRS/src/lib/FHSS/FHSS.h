#ifndef FHSS_H
#define FHSS_H

#include "random.h"
#include "tk86xx_api.h"

#define FREQ_SPREAD_SCALE 1
#define FHSS_SEQUENCE_LEN 256
#define ELRS_TLM_TEMPLATE_LEN 128

typedef struct {
    const char  *domain;
    uint32_t    freq_start;
    uint32_t    freq_stop;
    uint32_t    freq_count;
    uint32_t    freq_center;
} fhss_config_t;

extern volatile uint8_t FHSSptr;

extern uint16_t primaryBandCount;
extern uint32_t freq_spread;
extern uint8_t FHSSsequence[];
extern uint_fast8_t sync_channel;
extern const fhss_config_t *FHSSconfig;

void FHSSrandomiseFHSSsequence(uint32_t seed);
void FHSSrandomiseFHSSsequenceBuild(uint32_t seed, uint32_t freqCount, uint_fast8_t sync_channel, uint8_t *sequence);

static inline uint32_t FHSSgetMinimumFreq(void)
{
    return FHSSconfig->freq_start;
}

static inline uint32_t FHSSgetMaximumFreq(void)
{
    return FHSSconfig->freq_stop;
}

static inline uint32_t FHSSgetChannelCount(void)
{
    return FHSSconfig->freq_count;
}

static inline uint16_t FHSSgetSequenceCount(void)
{
    return primaryBandCount;
}

static inline uint32_t FHSSgetInitialFreq(void)
{
#if SENSI_TEST && !SENSI_TEST_PROFILE
    return 905300000; // Use a fixed frequency during sensitivity testing.
#else
    return FHSSconfig->freq_start + (sync_channel * freq_spread / FREQ_SPREAD_SCALE);
#endif
}

static inline uint8_t FHSSgetCurrIndex(void)
{
    return FHSSptr;
}

static inline uint8_t FHSSonSyncChannel(void)
{
    return FHSSsequence[FHSSptr] == sync_channel;
}

static inline void FHSSsetCurrIndex(const uint8_t value)
{
    FHSSptr = value % FHSSgetSequenceCount();
}

static inline uint32_t FHSSgetFreq(uint8_t slotIdx)
{
    slotIdx %= ELRS_TLM_TEMPLATE_LEN;

    return ((FHSSconfig->freq_start + (freq_spread * FHSSsequence[slotIdx] / FREQ_SPREAD_SCALE)) / 1000) * 1000;
}

static inline const char *FHSSgetRegulatoryDomain(void)
{
    return FHSSconfig->domain;
}

#endif
