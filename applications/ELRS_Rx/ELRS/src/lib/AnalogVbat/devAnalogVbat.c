#include "devAnalogVbat.h"

#if defined(USE_ANALOG_VBAT)

#include <stdbool.h>
#include "CRSF.h"
#include "telemetry.h"
#include "median.h"
#include "logging.h"
#include "tk86xx_api.h"

// Sample 5x samples over 500ms (unless SlowUpdate)
#if defined(DEBUG_VBAT_ADC)
#define VBAT_SAMPLE_INTERVAL    20U // faster updates in debug mode
#else
#define VBAT_SAMPLE_INTERVAL    100U
#endif

// static MedianAvgFilter_t vbatSmooth;
static uint8_t vbatUpdateScale;

/**
* Scale of the value returned by calc_scaled()
* Divide by this to convert from unscaled to original units
*/
// static size_t MedianAvgFilter_scale() 
// { 
//     return VBAT_SMOOTH_CNT - 2; 
// }

/**
* Calculate the MedianAvg but without dividing by count
* Useful for preserving precision when applying external scaling
*/
// static uint16_t MedianAvgFilter_calc_scaled()
// {
//     uint16_t minVal, maxVal, retVal;
//     maxVal = minVal = retVal = vbatSmooth._data[0];
//     // Find the minumum and maximum elements in the list
//     // while summing all the values
//     for (unsigned int i = 1; i < VBAT_SMOOTH_CNT; ++i)
//     {
//         uint16_t val = vbatSmooth._data[i];
//         retVal += val;
//         if (val < minVal)
//             minVal = val;
//         if (val > maxVal)
//             maxVal = val;
//     }
//     // Subtract out the min and max values to discard them
//     return (retVal - (minVal + maxVal));
// }

/**
* Adds a value to the accumulator, returns 0
* if the accumulator has filled a complete cycle
* of N elements
*/
// static unsigned int MedianAvgFilter_add(uint16_t item)
// {
//     vbatSmooth._data[vbatSmooth._counter] = item;
//     vbatSmooth._counter = (vbatSmooth._counter + 1) % VBAT_SMOOTH_CNT;
//     return vbatSmooth._counter;
// }

/**
* Resets the accumulator and position
*/
// static void MedianAvgFilter_clear()
// {
//     vbatSmooth._counter = 0;
//     memset(vbatSmooth._data, 0, sizeof(vbatSmooth._data));
// }

/**
* Calculate the MedianAvg
*/
// static uint16_t MedianAvgFilter_calc()
// {
//     return MedianAvgFilter_calc_scaled() / MedianAvgFilter_scale();
// }

/* Shameful externs */
extern Telemetry_t telemetry;

/**
 * @brief: Enable SlowUpdate mode to reduce the frequency Vbat telemetry is sent
 ***/
void Vbat_enableSlowUpdate(bool enable)
{
    vbatUpdateScale = enable ? 2 : 1;
}

static int start()
{
    // if (GPIO_ANALOG_VBAT == UNDEF_PIN)
    // {
    //     return DURATION_NEVER;
    // }
    vbatUpdateScale = 1;

    return VBAT_SAMPLE_INTERVAL;
}

static void reportVbat()
{
    // uint32_t adc = MedianAvgFilter_calc();

    uint8_t vbat = 0;
    // For negative offsets, anything between abs(OFFSET) and 0 is considered 0
    // if (ANALOG_VBAT_OFFSET < 0 && adc <= -ANALOG_VBAT_OFFSET)
    //     vbat = 0;
    // else
    //     vbat = ((int32_t)adc - ANALOG_VBAT_OFFSET) * 100 / ANALOG_VBAT_SCALE;
    Tk86xxGetVolt(&vbat);

    CRSF_MK_FRAME_T(crsf_sensor_battery_t) crsfbatt = { 0 };
    // Values are MSB first (BigEndian)
    crsfbatt.p.voltage = htobe16((uint16_t)(vbat * 100 * 100));
    // No sensors for current, capacity, or remaining available

    // Telemetry frames going to the radio must be addressed to RADIO_TRANSMITTER (0xEA)
    CRSF_SetHeaderAndCrc((uint8_t *)&crsfbatt, CRSF_FRAMETYPE_BATTERY_SENSOR, CRSF_FRAME_SIZE(sizeof(crsf_sensor_battery_t)), CRSF_ADDRESS_RADIO_TRANSMITTER);
    telemetry.AppendTelemetryPackage((uint8_t *)&crsfbatt);
    // DBGLN("report vbat");
}

static int timeout()
{
    if (/*GPIO_ANALOG_VBAT == UNDEF_PIN || */telemetry.GetCrsfBatterySensorDetected())
    {
        return DURATION_NEVER;
    }

    // uint32_t adc = analogRead(GPIO_ANALOG_VBAT);

    // unsigned int idx = vbatSmooth.add(adc);
    if (/*idx == 0 && */connectionState == connected)
        reportVbat();

    return VBAT_SAMPLE_INTERVAL * vbatUpdateScale;
}

device_t AnalogVbat_device = {
    .initialize = NULL,
    .start = start,
    .event = NULL,
    .timeout = timeout,
};

#endif
