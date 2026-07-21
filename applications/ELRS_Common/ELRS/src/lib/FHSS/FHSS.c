#include <string.h>
#include <stdbool.h>
#include "FHSS.h"
#include "logging.h"
#include "tk86xx_api.h"

const fhss_config_t domains[] = {
    // Keep all hopping channels inside the TX board's 902-928 MHz SAW
    // passband, with guard band on both filter skirts.
    {"SAW_915",  905300000, 925300000, 40, 915300000},
};

// Our table of FHSS frequencies. Define a regulatory domain to select the correct set for your location and radio
const fhss_config_t *FHSSconfig;

// Actual sequence of hops as indexes into the frequency list
uint8_t FHSSsequence[FHSS_SEQUENCE_LEN];

// Which entry in the sequence we currently are on
uint8_t volatile FHSSptr;

// Channel for sync packets and initial connection establishment
uint_fast8_t sync_channel;

// Frequency hop separation
uint32_t freq_spread;

uint16_t primaryBandCount;

void FHSSrandomiseFHSSsequence(const uint32_t seed)
{
    FHSSconfig = &domains[0];
    sync_channel = (FHSSconfig->freq_count / 2) + 1;
    freq_spread = (FHSSconfig->freq_stop - FHSSconfig->freq_start) * FREQ_SPREAD_SCALE / (FHSSconfig->freq_count - 1);
    primaryBandCount = (FHSS_SEQUENCE_LEN / FHSSconfig->freq_count) * FHSSconfig->freq_count;

    DBGLN("Setting %s Mode", FHSSconfig->domain);
    DBGLN("Number of FHSS frequencies = %u", FHSSconfig->freq_count);
    DBGLN("Sync channel = %u", sync_channel);
    DBGLN("Frequency spread = %u,start = %u, stop = %u", freq_spread, FHSSconfig->freq_start, FHSSconfig->freq_stop);

    uint32_t fhssSeed = seed;
#if SENSI_TEST && SENSI_TEST_PROFILE
    fhssSeed = 0x8620A55A;
#endif

    FHSSrandomiseFHSSsequenceBuild(fhssSeed, FHSSconfig->freq_count, sync_channel, FHSSsequence);
}

/**
Requirements:
1. 0 every n hops
2. No two repeated channels
3. Equal occurance of each (or as even as possible) of each channel
4. Pseudorandom

Approach:
  Fill the sequence array with the sync channel every FHSS_FREQ_CNT
  Iterate through the array, and for each block, swap each entry in it with
  another random entry, excluding the sync channel.

*/
void FHSSrandomiseFHSSsequenceBuild(const uint32_t seed, uint32_t freqCount, uint_fast8_t syncChannel, uint8_t *inSequence)
{
    // reset the pointer (otherwise the tests fail)
    FHSSptr = 0;
    rngSeed(seed);

    // initialize the sequence array
    for (uint16_t i = 0; i < FHSSgetSequenceCount(); i++)
    {
        if (i % freqCount == 0) {
            inSequence[i] = syncChannel;
        } else if (i % freqCount == syncChannel) {
            inSequence[i] = 0;
        } else {
            inSequence[i] = i % freqCount;
        }
    }

    for (uint16_t i = 0; i < FHSSgetSequenceCount(); i++)
    {
        // if it's not the sync channel
        if (i % freqCount != 0)
        {
            uint8_t offset = (i / freqCount) * freqCount;   // offset to start of current block
            uint8_t rand = rngN(freqCount - 1) + 1;         // random number between 1 and FHSS_FREQ_CNT

            // switch this entry and another random entry in the same block
            uint8_t temp = inSequence[i];
            inSequence[i] = inSequence[offset+rand];
            inSequence[offset+rand] = temp;
        }
    }

    // output FHSS sequence (print index, channel index and corresponding frequency)
}
