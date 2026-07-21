#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "stubborn_sender.h"
#include "helpers.h"
#include "logging.h"

static StubbornSender_t *pSender = NULL;
static void StubbornSender_setMaxPackageIndex(uint8_t maxPackageIndex)
{
    if (pSender->maxPackageIndex != maxPackageIndex)
    {
        pSender->maxPackageIndex = maxPackageIndex;
        pSender->ResetState();
    }
}

static void StubbornSender_ResetState(void)
{
    if (pSender == NULL) {
        ERRLN("StubbornSender not initialized");
        return;
    }
    pSender->bytesLastPayload = 0;
    pSender->currentOffset = 0;
    pSender->currentPackage = 1;
    pSender->telemetryConfirmExpectedValue = true;
    pSender->waitCount = 0;
    // 80 corresponds to UpdateTelemetryRate(ANY, 2, 1), which is what the TX uses in boost mode
    pSender->maxWaitCount = 80;
    pSender->senderState = SENDER_IDLE;
}

/***
 * Queues a message to send, will abort the current message if one is currently being transmitted
 ***/
static void StubbornSender_SetDataToTransmit(uint8_t* dataToTransmit, uint8_t lengthToTransmit)
{
    if (pSender == NULL) {
        ERRLN("StubbornSender not initialized");
        return;
    }
    pSender->length = lengthToTransmit;
    pSender->data = dataToTransmit;
    pSender->currentOffset = 0;
    pSender->currentPackage = 1;
    pSender->waitCount = 0;
    pSender->senderState = (pSender->senderState == SENDER_IDLE) ? SEND_PENDING : RESYNC_THEN_SEND;
}

/**
 * @brief: Copy up to maxLen bytes from the current package to outData
 * @returns: packageIndex
 ***/
static uint8_t StubbornSender_GetCurrentPayload(uint8_t *outData, uint8_t maxLen)
{
    if (pSender == NULL) {
        ERRLN("StubbornSender not initialized");
        return 0;
    }
    uint8_t packageIndex;

    pSender->bytesLastPayload = 0;
    switch (pSender->senderState) {
    case RESYNC:
    case RESYNC_THEN_SEND:
        packageIndex = pSender->maxPackageIndex;
        break;
    case SEND_PENDING:
        // This package can now be acked
        pSender->senderState = SENDING;
        // fallthrough
    case SENDING:
        {
            pSender->bytesLastPayload = MIN((uint8_t)(pSender->length - pSender->currentOffset), maxLen);
            // If this is the last data chunk, and there has been at least one other packet
            // skip the blank packet needed for WAIT_UNTIL_NEXT_CONFIRM
            if (pSender->currentPackage > 1 && (pSender->currentOffset + pSender->bytesLastPayload) >= pSender->length)
                packageIndex = 0;
            else
                packageIndex = pSender->currentPackage;

            memcpy(outData, &pSender->data[pSender->currentOffset], pSender->bytesLastPayload);
        }
        break;
    default:
        packageIndex = 0;
    }

    return packageIndex;
}

static void StubbornSender_ConfirmCurrentPayload(bool telemetryConfirmValue)
{
     // DBGLN("confirm: %d, expected: %d", telemetryConfirmValue, pSender->telemetryConfirmExpectedValue);
    stubborn_sender_state_e nextSenderState = pSender->senderState;

    // DBGLN("telemetryConfirmExpectedValue:%d, value:%d", pSender->telemetryConfirmExpectedValue, telemetryConfirmValue);
    switch (pSender->senderState)
    {
    case SENDING:
        if (telemetryConfirmValue != pSender->telemetryConfirmExpectedValue)
        {
            pSender->waitCount++;
            if (pSender->waitCount > pSender->maxWaitCount)
            {
                pSender->telemetryConfirmExpectedValue = !telemetryConfirmValue;
                nextSenderState = RESYNC;
            }
            break;
        }

        pSender->currentOffset += pSender->bytesLastPayload;
        if (pSender->currentOffset >= pSender->length)
        {
            // A 0th packet is always requred so the reciver can
            // differentiate a new send from a resend, if this is
            // the first packet acked, send another, else IDLE
            if (pSender->currentPackage == 1)
                nextSenderState = WAIT_UNTIL_NEXT_CONFIRM;
            else
                nextSenderState = SENDER_IDLE;
        }

        pSender->currentPackage++;
        pSender->telemetryConfirmExpectedValue = !pSender->telemetryConfirmExpectedValue;
        pSender->waitCount = 0;
        break;

    case RESYNC:
    case RESYNC_THEN_SEND:
    case WAIT_UNTIL_NEXT_CONFIRM:
        if (telemetryConfirmValue == pSender->telemetryConfirmExpectedValue)
        {
            nextSenderState = (pSender->senderState == RESYNC_THEN_SEND) ? SENDING : SENDER_IDLE;
            pSender->telemetryConfirmExpectedValue = !telemetryConfirmValue;
        }
        // switch to resync if tx does not confirm value fast enough
        else if (pSender->senderState == WAIT_UNTIL_NEXT_CONFIRM)
        {
            pSender->waitCount++;
            if (pSender->waitCount > pSender->maxWaitCount)
            {
                pSender->telemetryConfirmExpectedValue = !telemetryConfirmValue;
                nextSenderState = RESYNC;
            }
        }
        break;

    case SEND_PENDING:
        // TelemetryConfirm acks are not accepted before sending
        // fallthrough
    case SENDER_IDLE:
        break;
    }

    pSender->senderState = nextSenderState;
}

/*
 * Called when the telemetry ratio or air rate changes, calculate
 * the new threshold for how many times the telemetryConfirmValue
 * can be wrong in a row before giving up and going to RESYNC
 */
void StubbornSender_UpdateTelemetryRate(uint16_t airRate, uint8_t tlmRatio, uint8_t tlmBurst)
{
    if (pSender == NULL) {
        ERRLN("StubbornSender not initialized");
        return;
    }

    // consipicuously unused airRate parameter, the wait count is strictly based on number
    // of packets, not time between the telemetry packets, or a wall clock timeout
    (void)airRate;
    // The expected number of packet periods between telemetry packets
    uint32_t packsBetween = tlmRatio * (1 + tlmBurst) / tlmBurst;
    pSender->maxWaitCount = packsBetween * SSENDER_MAX_MISSED_PACKETS;
}

static bool StubbornSender_IsActive()
{ 
    if (pSender == NULL) {
        ERRLN("StubbornSender not initialized");
        return false;
    }
    return pSender->senderState != SENDER_IDLE; 
}

void StubbornSender_Init(StubbornSender_t *sender)
{
    pSender = sender;
    sender->IsActive = StubbornSender_IsActive;
    sender->SetDataToTransmit = StubbornSender_SetDataToTransmit;
    sender->ResetState = StubbornSender_ResetState;
    sender->GetCurrentPayload = StubbornSender_GetCurrentPayload;
    sender->setMaxPackageIndex = StubbornSender_setMaxPackageIndex;
    sender->ConfirmCurrentPayload = StubbornSender_ConfirmCurrentPayload;
    sender->UpdateTelemetryRate = StubbornSender_UpdateTelemetryRate;
}
