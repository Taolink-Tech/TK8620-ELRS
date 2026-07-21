#include "dynpower.h"

#include <stddef.h>
#include <stdbool.h>

#include "common.h"
#include "helpers.h"
#include "MeanAccumulator.h"
#include "CRSFHandset.h"

// LQ-based boost defines
#define DYNPOWER_LQ_BOOST_THRESH_DIFF 20  // If LQ drops suddenly for this amount (relative), immediately boost to the max power configured.
#define DYNPOWER_LQ_BOOST_THRESH_MIN  50  // If LQ is below this value (absolute), immediately boost to the max power configured.
#define DYNPOWER_LQ_MOVING_AVG_K      8   // Number of previous values for calculating moving average. Best with power of 2.
#define DYNPOWER_LQ_THRESH_UP         85  // Below this LQ, the RSSI/SNR code will increase the power if RSSI/SNR did nothing

// RSSI-based increment defines
#define DYNPOWER_RSSI_CNT 8               // Use a longer average on TK8620 because RSSI is estimated from the PHY, not read from a radio RSSI register.
#define DYNPOWER_RSSI_THRESH_UP 15        // RSSI < (Sensitivity+Up) -> raise power

// SNR-based increment defines
#define DYNPOWER_LQ_THRESH_DN 95          // Min LQ for lowering power using SNR-based power lowering

#define DYNPOWER_MOVAVG_SHIFT 16

typedef struct {
    uint32_t shiftedVal;
} MovingAvg_t;

static void MovingAvg_init(MovingAvg_t *m, uint32_t v)
{
    m->shiftedVal = (v << DYNPOWER_MOVAVG_SHIFT);
}

static void MovingAvg_add(MovingAvg_t *m, uint32_t v)
{
    m->shiftedVal = ((DYNPOWER_LQ_MOVING_AVG_K - 1U) * m->shiftedVal + (v << DYNPOWER_MOVAVG_SHIFT)) / DYNPOWER_LQ_MOVING_AVG_K;
}

static uint32_t MovingAvg_get(const MovingAvg_t *m)
{
    return (m->shiftedVal >> DYNPOWER_MOVAVG_SHIFT);
}

static MovingAvg_t dynpower_mavg_lq;
static MeanAccumulator_t dynpower_mean_rssi;
static int8_t dynpower_updated;
static uint32_t dynpower_last_linkstats_millis;

static void DynamicPower_SetToConfigPower(void)
{
    // DBGLN("setToConfigPwr");
    POWERMGNT_setPower((PowerLevels_e)txConfig.GetPower());
}

void DynamicPower_Init(void)
{
    MovingAvg_init(&dynpower_mavg_lq, 100U);
    dynpower_updated = DYNPOWER_UPDATE_NOUPDATE;
    dynpower_last_linkstats_millis = 0;
    MeanAccumulator_reset(&dynpower_mean_rssi);
}

void DynamicPower_TelemetryUpdate(int8_t snr)
{
    dynpower_updated = snr;
}

void DynamicPower_Update(uint32_t now)
{
    const int8_t snr = dynpower_updated;
    dynpower_updated = DYNPOWER_UPDATE_NOUPDATE;

    const bool newTlmAvail = (snr > DYNPOWER_UPDATE_MISSED);
    const bool lastTlmMissed = (snr == DYNPOWER_UPDATE_MISSED);

    elrsLinkStatistics_t *ls = CRSF_GetLinkStatistics();
    const uint8_t activeAnt = ls->crsfLinkStatistics.active_antenna;
    const uint8_t rssiRaw = (activeAnt == 0) ? ls->crsfLinkStatistics.uplink_RSSI_1 : ls->crsfLinkStatistics.uplink_RSSI_2;
    const int8_t rssiDbm = (int8_t)rssiRaw;

    // Power is too strong and saturates the RX LNA
    if (newTlmAvail && (rssiDbm >= -5))
    {
        DBGLN("-power (overload)");
        POWERMGNT_decPower();
    }

    // When not using dynamic power, return here
    if (!txConfig.GetDynamicPower())
    {
        // if RSSI is dropped enough, inc power back to the configured power
        if (newTlmAvail && (rssiDbm <= -20) && (POWERMGNT_currPower() < (PowerLevels_e)txConfig.GetPower()))
        {
            DynamicPower_SetToConfigPower();
        }
        return;
    }

    // ============= DYNAMIC_POWER_BOOST: Switch-triggered power boost up ==============
    // Or if telemetry is lost while armed (done up here because dynpower_updated is only updated on telemetry)
    const uint8_t boostChannel = txConfig.GetBoostChannel();
    const bool armed = (CRSFHandset.handset != NULL) ? CRSFHandset.handset->IsArmed() : false;

    if ((connectionState == disconnected && armed) ||
        (boostChannel && (CRSF_to_BIT(ChannelData[AUX9 + boostChannel - 1]) == 0)))
    {
        DynamicPower_SetToConfigPower();
        return;
    }

    // How much available power is left for incremental increases
    const uint8_t cfgPower = txConfig.GetPower();
    const uint8_t currPower = (uint8_t)POWERMGNT_currPower();
    uint8_t powerHeadroom = (cfgPower > currPower) ? (cfgPower - currPower) : 0;

    if (lastTlmMissed)
    {
        // If armed and missing telemetry, raise the power, but only after the first LinkStats is missed (which come
        // at most every 512ms). This delays the first increase, then will bump it once for each missed TLM after that.
        if (armed && (powerHeadroom > 0))
        {
            uint32_t linkstatsInterval = (uint32_t)ExpressLRS_currTlmDenom * (uint32_t)ExpressLRS_currAirRate_Modparams->interval / 500U;
            if (linkstatsInterval < 512U) linkstatsInterval = 512U;
            if ((dynpower_last_linkstats_millis != 0) && ((now - dynpower_last_linkstats_millis) > (linkstatsInterval + 2U)))
            {
                DBGLN("+power (tlm)");
                POWERMGNT_incPower();
            }
        }
        return;
    }

    // If no new telemetry, no new LQ/SNR/RSSI to use for adjustment
    if (!newTlmAvail)
    {
        return;
    }
    dynpower_last_linkstats_millis = now;

    // ============= LQ-based power boost up ==============
    const uint32_t lq_current = ls->crsfLinkStatistics.uplink_Link_quality;
    const uint32_t lq_avg = MovingAvg_get(&dynpower_mavg_lq);
    const int32_t lq_diff = (int32_t)lq_avg - (int32_t)lq_current;
    MovingAvg_add(&dynpower_mavg_lq, lq_current);

    // If LQ drops quickly or critically low, immediately boost to the configured max power.
    if ((lq_diff >= DYNPOWER_LQ_BOOST_THRESH_DIFF) || (lq_current <= DYNPOWER_LQ_BOOST_THRESH_MIN))
    {
        DynamicPower_SetToConfigPower();
        return;
    }

    const PowerLevels_e startPowerLevel = POWERMGNT_currPower();
    if (ExpressLRS_currAirRate_RFperfParams->DynpowerSnrThreshUp == DYNPOWER_SNR_THRESH_NONE)
    {
        // ============= RSSI-based power increment ==============
        MeanAccumulator_add(&dynpower_mean_rssi, rssiDbm);

        if (MeanAccumulator_getCount(&dynpower_mean_rssi) >= DYNPOWER_RSSI_CNT)
        {
            const int32_t expected_RXsensitivity = ExpressLRS_currAirRate_RFperfParams->RXsensitivity;
            const int8_t rssi_inc_threshold = (int8_t)(expected_RXsensitivity + DYNPOWER_RSSI_THRESH_UP);
            const int8_t avg_rssi = MeanAccumulator_mean(&dynpower_mean_rssi); // resets it too

            if ((avg_rssi < rssi_inc_threshold) && (powerHeadroom > 0))
            {
                DBGLN("+power (rssi)");
                POWERMGNT_incPower();
            }
        }
    }
    else
    {
        // ============= SNR-based power increment ==============
        const int8_t snrThreshDn = ExpressLRS_currAirRate_RFperfParams->DynpowerSnrThreshDn;
        const int8_t snrThreshUp = ExpressLRS_currAirRate_RFperfParams->DynpowerSnrThreshUp;

        // Decrease the power if SNR above threshold and LQ is good
        if ((snr >= snrThreshDn) && (lq_avg >= DYNPOWER_LQ_THRESH_DN))
        {
            DBGLN("-power (snr)");
            POWERMGNT_decPower();
        }

        // Increase the power for each ~2dB below the threshold
        int8_t snrTmp = snr;
        while ((snrTmp <= snrThreshUp) && (powerHeadroom > 0))
        {
            DBGLN("+power (snr)");
            POWERMGNT_incPower();
            snrTmp += 2;
            --powerHeadroom;
        }
    }

    // If instant LQ is low, but the SNR/RSSI did nothing, inc power by one step
    if ((powerHeadroom > 0) && (startPowerLevel == POWERMGNT_currPower()) && (lq_current <= DYNPOWER_LQ_THRESH_UP))
    {
        DBGLN("+power (lq)");
        POWERMGNT_incPower();
    }
}

