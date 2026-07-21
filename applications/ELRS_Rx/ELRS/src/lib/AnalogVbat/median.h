#pragma once
#include <stdint.h>

#define VBAT_SMOOTH_CNT         5
/**
 * Throws out the highest and lowest values then averages what's left
 */
typedef struct {
    /**
     * Operator to just assign as type
     */
    // operator uint16_t() const { return calc(); }

    uint16_t _data[VBAT_SMOOTH_CNT];
    unsigned int _counter;
} MedianAvgFilter_t;