#pragma once
#include <stdint.h>
#include <stdbool.h>

/////////// Super Simple Fixed Point Lowpass ////////////////

typedef struct {
    int32_t SmoothDataINT;
    int32_t SmoothDataFP;
    int32_t Beta;     // Length = 16
    int32_t FP_Shift; //Number of fractional bits
    bool NeedReset;  // wait for the first data to upcoming.
} LPF_t;

static void LPF_init(LPF_t *LPF, int32_t Indata)
{
    LPF->NeedReset = false;

    LPF->SmoothDataINT = Indata;
    LPF->SmoothDataFP = LPF->SmoothDataINT << LPF->FP_Shift;
}

static int32_t LPF_update(LPF_t *LPF, int32_t Indata)
{
    if (LPF->NeedReset)
    {
        LPF_init(LPF, Indata);
        return LPF->SmoothDataINT;
    }

    int RawData;
    RawData = Indata;
    RawData <<= LPF->FP_Shift; // Shift to fixed point
    LPF->SmoothDataFP = (LPF->SmoothDataFP << LPF->Beta) - LPF->SmoothDataFP;
    LPF->SmoothDataFP += RawData;
    LPF->SmoothDataFP >>= LPF->Beta;
    // Don't do the following shift if you want to do further
    // calculations in fixed-point using SmoothData
    LPF->SmoothDataINT = LPF->SmoothDataFP >> LPF->FP_Shift;
    return LPF->SmoothDataINT;
}

static __attribute__((unused)) void LPF_reset(LPF_t *LPF)
{
    LPF->NeedReset = true;
}

static __attribute__((unused)) int32_t LPF_value(LPF_t *LPF)
{ 
    return LPF->SmoothDataINT; 
}
