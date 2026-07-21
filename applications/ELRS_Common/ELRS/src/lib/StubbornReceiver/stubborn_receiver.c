#include <stddef.h>
#include <string.h>
#include "stubborn_receiver.h"
#include "logging.h"

static StubbornReceiver_t* pReceiver = NULL;

static void StubbornReceiver_setMaxPackageIndex(uint8_t maxPackageIndex)
{
    if (!pReceiver) {
        ERRLN("pReceiver is NULL");
        return;
    }

    if (pReceiver->maxPackageIndex != maxPackageIndex)
    {
        pReceiver->maxPackageIndex = maxPackageIndex;
        pReceiver->ResetState();
    }
}

static void StubbornReceiver_ResetState(void)
{
    if (!pReceiver) {
        ERRLN("pReceiver is NULL");
        return;
    }
    pReceiver->currentPackage = 1;
    pReceiver->currentOffset = 0;
    pReceiver->telemetryConfirm = false;
}

static bool StubbornReceiver_GetCurrentConfirm(void)
{
    if (!pReceiver) {
        ERRLN("pReceiver is NULL");
        return false;
    }
    return pReceiver->telemetryConfirm;
}

static void StubbornReceiver_SetDataToReceive(uint8_t* dataToReceive, uint8_t maxLength)
{
    if (!pReceiver) {
        ERRLN("pReceiver is NULL");
        return;
    }
    pReceiver->length = maxLength;
    pReceiver->data = dataToReceive;
    pReceiver->currentPackage = 1;
    pReceiver->currentOffset = 0;
    pReceiver->finishedData = false;
}

static void StubbornReceiver_ReceiveData(uint8_t const packageIndex, uint8_t const * const receiveData, uint8_t dataLen)
{
    if (!pReceiver) {
        ERRLN("pReceiver is NULL");
        return;
    }

    // DBGLN("maxPackageIndex:%d", pReceiver->maxPackageIndex);
    // Resync
    if (packageIndex == pReceiver->maxPackageIndex)
    {
        // DBGLN("Resync:");
        pReceiver->telemetryConfirm = !pReceiver->telemetryConfirm;
        pReceiver->currentPackage = 1;
        pReceiver->currentOffset = 0;
        pReceiver->finishedData = false;
        // DBGLN("telemetryConfirm 1 :%d", pReceiver->telemetryConfirm);
        return;
    }

    if (pReceiver->finishedData)
    {
        // DBGLN("FinishedData");
        return;
    }

    // DBGLN("packageIndex:%d, currentPackage:%d", packageIndex, pReceiver->currentPackage);
    bool acceptData = false;
    // If this is the last package, accept as being complete
    if (packageIndex == 0 && pReceiver->currentPackage > 1)
    {
        // PackageIndex 0 (the final packet) can also contain data
        acceptData = true;
        pReceiver->finishedData = true;
    }
    // If this package is the expected index, accept and advance index
    else if (packageIndex == pReceiver->currentPackage)
    {
        acceptData = true;
    }
    // If this is the first package from the sender, and we're mid-receive
    // assume the sender has restarted without resync or is freshly booted
    // skip the resync process entirely and just pretend this is a fresh boot too
    else if (packageIndex == 1 && pReceiver->currentPackage > 1)
    {
        pReceiver->currentPackage = 1;
        pReceiver->currentOffset = 0;
        acceptData = true;
    }

    if (acceptData)
    {
        // DBGLN("AcceptData");
        uint8_t remaining = (uint8_t)(pReceiver->length - pReceiver->currentOffset);
        uint8_t len = (remaining < dataLen) ? remaining : dataLen;
        if (len) {
            memcpy(&pReceiver->data[pReceiver->currentOffset], receiveData, len);
        }
        pReceiver->currentPackage++;
        pReceiver->currentOffset += len;
        pReceiver->telemetryConfirm = !pReceiver->telemetryConfirm;
    }
    // DBGLN("telemetryConfirm:%d", pReceiver->telemetryConfirm);
}

static bool StubbornReceiver_HasFinishedData(void)
{
    if (!pReceiver) {
        ERRLN("pReceiver is NULL");
        return false;
    }
    return pReceiver->finishedData;
}

static void StubbornReceiver_Unlock(void)
{
    if (!pReceiver) {
        ERRLN("pReceiver is NULL");
        return;
    }
    if (pReceiver->finishedData)
    {
        pReceiver->currentPackage = 1;
        pReceiver->currentOffset = 0;
        pReceiver->finishedData = false;
    }
}

void StubbornReceiver_Init(StubbornReceiver_t* receiver)
{
    pReceiver = receiver;
    /* set function pointers to C implementations */
    pReceiver->ResetState = StubbornReceiver_ResetState;
    pReceiver->GetCurrentConfirm = StubbornReceiver_GetCurrentConfirm;
    pReceiver->SetDataToReceive = StubbornReceiver_SetDataToReceive;
    pReceiver->ReceiveData = StubbornReceiver_ReceiveData;
    pReceiver->HasFinishedData = StubbornReceiver_HasFinishedData;
    pReceiver->Unlock = StubbornReceiver_Unlock;
    pReceiver->setMaxPackageIndex = StubbornReceiver_setMaxPackageIndex;

    pReceiver->ResetState();
    pReceiver->data = NULL;
    pReceiver->length = 0;
}