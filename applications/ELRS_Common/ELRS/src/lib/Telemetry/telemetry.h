#pragma once

#include <stdbool.h>
#include "CRSF.h"
#include "FIFO.h"

typedef FIFO_t TelemetryFifo_t;

enum CustomTelemSubTypeID {
    CRSF_AP_CUSTOM_TELEM_SINGLE_PACKET_PASSTHROUGH = 0xF0,
    CRSF_AP_CUSTOM_TELEM_STATUS_TEXT = 0xF1,
    CRSF_AP_CUSTOM_TELEM_MULTI_PACKET_PASSTHROUGH = 0xF2,
};

typedef enum {
    TELEMETRY_IDLE = 0,
    RECEIVING_LENGTH,
    RECEIVING_DATA
} telemetry_state_s;

typedef struct {
    bool (*RXhandleUARTin)(uint8_t data);
    void (*ResetState)(void);
    bool (*ShouldCallBootloader)(void);
    bool (*ShouldCallEnterBind)(void);
    bool (*ShouldCallUpdateModelMatch)(void);
    bool (*ShouldSendDeviceFrame)(void);
    void (*SetCrsfBatterySensorDetected)(void);
    bool (*GetCrsfBatterySensorDetected)(void);
    void (*SetCrsfBaroSensorDetected)(void);
    uint8_t (*GetUpdatedModelMatch)(void);
    bool (*GetNextPayload)(uint8_t* nextPayloadSize, uint8_t *payloadData);
    void (*AppendTelemetryPackage)(uint8_t *package);
    TelemetryFifo_t messagePayloads;
    uint8_t CRSFinBuffer[CRSF_MAX_PACKET_LEN];
    telemetry_state_s telemetry_state;
    uint8_t currentTelemetryByte;
    uint8_t prioritizedCount;
    bool callBootloader;
    bool callEnterBind;
    bool callUpdateModelMatch;
    bool sendDeviceFrame;
    bool crsfBatterySensorDetected;
    bool crsfBaroSensorDetected;
    uint8_t modelMatchId;
} Telemetry_t;

void Telemetry_Init(Telemetry_t *telemetry);
