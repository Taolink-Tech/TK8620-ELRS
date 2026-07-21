#pragma once
#include <stdint.h>

typedef struct {
    int32_t _accumulator;
    int32_t _count;
    int8_t _previousMean;
} MeanAccumulator_t;

static void MeanAccumulator_add(MeanAccumulator_t *meanAccumulator, int8_t val)
{
    meanAccumulator->_accumulator += val;
    meanAccumulator->_count++;
}

static void MeanAccumulator_reset(MeanAccumulator_t *meanAccumulator)
{
    meanAccumulator->_accumulator = 0;
    meanAccumulator->_count = 0;
}

static int8_t MeanAccumulator_mean(MeanAccumulator_t *meanAccumulator)
{
    if (meanAccumulator->_count)
    {
        meanAccumulator->_previousMean = meanAccumulator->_accumulator / meanAccumulator->_count;
        MeanAccumulator_reset(meanAccumulator);

        return meanAccumulator->_previousMean;
    }
    return -16;
} 

static size_t MeanAccumulator_getCount(MeanAccumulator_t *meanAccumulator)
{
    return meanAccumulator->_count;
}