#include <stddef.h>
#include "telemetry.h"
#include "logging.h"
#include "crc.h"
#include "helpers.h"
#include "FIFO.h"
#include "tk86xx_platform.h"

// #include <functional>

#define BIT(a) (1 << (a))
#define CRSF_UART_FRAME_TIMEOUT_MS 100U

#if defined(__GNUC__)
extern Telemetry_t telemetry __attribute__((weak));
#else
extern Telemetry_t telemetry;
#endif

// Size byte in FIFO contains bit to indicate if the frame is deleted
#define IS_DEL(size) (size & BIT(7))
#define SET_DEL(size) (size | BIT(7))
#define SIZE(size) (size & ~BIT(7))

typedef enum {
    ACTION_NEXT,       // continue searching queue for other messages
    ACTION_IGNORE,     // the message matches; ignore the new one
    ACTION_OVERWRITE,  // overwrite the queued message with the new one
    ACTION_APPEND      // append the message to the end of the queue
} action_e;

typedef action_e (*comparator_t)(const crsf_header_t *newMessage, const TelemetryFifo_t *payloads, uint16_t queuePosition);

static uint32_t s_lastTelemetryByteMillis = 0;

// For broadcast messages that have a 'source_id' as the first byte of the payload.
static action_e sourceId(const crsf_header_t *newMessage, const TelemetryFifo_t *payloads, const uint16_t queuePosition)
{
    if (get((FIFO_t *)payloads, queuePosition + CRSF_TELEMETRY_TYPE_INDEX + 1) == newMessage->payload[0])
    {
        return ACTION_OVERWRITE;
    }
    return ACTION_NEXT;
}

// Comparator for Ardupilot Status Text message
static action_e statusText(const crsf_header_t *newMessage, const TelemetryFifo_t *payloads, const uint16_t queuePosition)
{
    if (get((FIFO_t *)payloads, queuePosition + CRSF_TELEMETRY_TYPE_INDEX + 1) == CRSF_AP_CUSTOM_TELEM_STATUS_TEXT &&
        newMessage->payload[0] == CRSF_AP_CUSTOM_TELEM_STATUS_TEXT)
    {
        return ACTION_OVERWRITE;
    }
    return ACTION_NEXT;
}

static action_e extended_dest_origin(const crsf_header_t *newMessage, const TelemetryFifo_t *payloads, const uint16_t queuePosition)
{
    if (get((FIFO_t *)payloads, queuePosition + 3) == ((crsf_ext_header_t *)newMessage)->dest_addr &&
        get((FIFO_t *)payloads, queuePosition + 4) == ((crsf_ext_header_t *)newMessage)->orig_addr)
    {
        return ACTION_OVERWRITE;
    }
    return ACTION_NEXT;
}

static action_e comp_OVERWRITE(const crsf_header_t *newMessage, const TelemetryFifo_t *payloads, const uint16_t queuePosition)
{
    return ACTION_OVERWRITE;
}

static struct
{
    crsf_frame_type_e type;
    comparator_t comparator;
} comparators[] = {
    {CRSF_FRAMETYPE_RPM, sourceId},
    {CRSF_FRAMETYPE_TEMP, sourceId},
    {CRSF_FRAMETYPE_CELLS, sourceId},
    {CRSF_FRAMETYPE_ARDUPILOT_RESP, statusText},
    {CRSF_FRAMETYPE_DEVICE_INFO, extended_dest_origin},
    {CRSF_FRAMETYPE_PARAMETER_SETTINGS_ENTRY, comp_OVERWRITE },
};

inline bool isPrioritised(const crsf_frame_type_e frameType)
{
    return frameType >= CRSF_FRAMETYPE_DEVICE_PING && frameType <= CRSF_FRAMETYPE_PARAMETER_WRITE;
}

static bool Telemetry_ShouldCallBootloader(void)
{
    bool bootloader = telemetry.callBootloader;
    telemetry.callBootloader = false;
    return bootloader;
}

static bool Telemetry_ShouldCallEnterBind(void)
{
    bool enterBind = telemetry.callEnterBind;
    telemetry.callEnterBind = false;
    return enterBind;
}

static bool Telemetry_ShouldCallUpdateModelMatch(void)
{
    bool updateModelMatch = telemetry.callUpdateModelMatch;
    telemetry.callUpdateModelMatch = false;
    return updateModelMatch;
}

static bool Telemetry_ShouldSendDeviceFrame(void)
{
    bool deviceFrame = telemetry.sendDeviceFrame;
    telemetry.sendDeviceFrame = false;
    return deviceFrame;
}

static void Telemetry_SetCrsfBatterySensorDetected(void)
{
    telemetry.crsfBatterySensorDetected = true;
}

static void CheckCrsfBatterySensorDetected()
{
    if (telemetry.CRSFinBuffer[CRSF_TELEMETRY_TYPE_INDEX] == CRSF_FRAMETYPE_BATTERY_SENSOR)
    {
        // DBGLN("CrsfBatterySensorDetected");
        Telemetry_SetCrsfBatterySensorDetected();
    }
}

static void Telemetry_SetCrsfBaroSensorDetected(void)
{
    telemetry.crsfBaroSensorDetected = true;
}

static void CheckCrsfBaroSensorDetected()
{
    if (telemetry.CRSFinBuffer[CRSF_TELEMETRY_TYPE_INDEX] == CRSF_FRAMETYPE_BARO_ALTITUDE ||
        telemetry.CRSFinBuffer[CRSF_TELEMETRY_TYPE_INDEX] == CRSF_FRAMETYPE_VARIO)
    {
        // DBGLN("CrsfBaroSensorDetected");
        Telemetry_SetCrsfBaroSensorDetected();
    }
}

static void Telemetry_ResetState()
{
    telemetry.telemetry_state = TELEMETRY_IDLE;
    telemetry.currentTelemetryByte = 0;
    telemetry.prioritizedCount = 0;
    s_lastTelemetryByteMillis = 0;
    // messagePayloads.flush();
}

static bool Telemetry_RXhandleUARTin(uint8_t data)
{
    const uint32_t now = millis();
    if (telemetry.telemetry_state != TELEMETRY_IDLE &&
        (uint32_t)(now - s_lastTelemetryByteMillis) > CRSF_UART_FRAME_TIMEOUT_MS)
    {
        telemetry.telemetry_state = TELEMETRY_IDLE;
        telemetry.currentTelemetryByte = 0;
    }
    s_lastTelemetryByteMillis = now;

    switch (telemetry.telemetry_state) {
        case TELEMETRY_IDLE:
            // Telemetry from Betaflight/iNav starts with CRSF_SYNC_BYTE (CRSF_ADDRESS_FLIGHT_CONTROLLER)
            // from a TX module it will be addressed to CRSF_ADDRESS_RADIO_TRANSMITTER (RX used as a relay)
            // and things addressed to CRSF_ADDRESS_CRSF_RECEIVER I guess we should take too since that's us, but we'll just forward them
            if (data == CRSF_SYNC_BYTE || data == CRSF_ADDRESS_RADIO_TRANSMITTER || data == CRSF_ADDRESS_CRSF_RECEIVER)
            {
                telemetry.currentTelemetryByte = 0;
                telemetry.telemetry_state = RECEIVING_LENGTH;
                telemetry.CRSFinBuffer[0] = data;
                // DBG("SYNC BYTE: %02X\n", data);
            } else {
                return false;
            }

            break;
        case RECEIVING_LENGTH:
            if (data < 2 || data > (CRSF_MAX_PACKET_LEN - CRSF_FRAME_NOT_COUNTED_BYTES))
            {
                telemetry.telemetry_state = TELEMETRY_IDLE;
                return false;
            }
            else
            {
                telemetry.telemetry_state = RECEIVING_DATA;
                telemetry.CRSFinBuffer[CRSF_TELEMETRY_LENGTH_INDEX] = data;
                // DBG("LENGTH: %02X\n", data);
            }

            break;
        case RECEIVING_DATA:
            telemetry.CRSFinBuffer[telemetry.currentTelemetryByte + CRSF_FRAME_NOT_COUNTED_BYTES] = data;
            telemetry.currentTelemetryByte++;
            if (telemetry.CRSFinBuffer[CRSF_TELEMETRY_LENGTH_INDEX] == telemetry.currentTelemetryByte)
            {
                // exclude first bytes (sync byte + length), skip last byte (submitted crc)
                uint8_t crc = GENERIC_CRC8Calc(telemetry.CRSFinBuffer + CRSF_FRAME_NOT_COUNTED_BYTES, telemetry.CRSFinBuffer[CRSF_TELEMETRY_LENGTH_INDEX] - CRSF_TELEMETRY_CRC_LENGTH, 0);
                telemetry.telemetry_state = TELEMETRY_IDLE;
                // DBG("calculated CRC: %02X, received CRC: %02X\n", crc, data);
                if (data == crc)
                {
                    // DBG("CRC OK\n");
                    telemetry.AppendTelemetryPackage(telemetry.CRSFinBuffer);

                    // Special case to check here and not in AppendTelemetryPackage(). devAnalogVbat and vario sends
                    // direct to AppendTelemetryPackage() and we want to detect packets only received through serial.
                    CheckCrsfBatterySensorDetected();
                    CheckCrsfBaroSensorDetected();

                    return true;
                }
                return false;
            }

            break;
    }

    return true;
}

/**
 * @brief: Check the CRSF frame for commands that should not be passed on
 * @return: true if packet was internal and should not be processed further
*/
static bool Telemetry_processInternalTelemetryPackage(uint8_t *package)
{
    const crsf_ext_header_t *header = (crsf_ext_header_t *)package;

    if (header->type == CRSF_FRAMETYPE_COMMAND)
    {
        // Non CRSF, dest=b src=l -> reboot to bootloader
        if (package[3] == 'b' && package[4] == 'l')
        {
            telemetry.callBootloader = true;
            return true;
        }
        // 1. Non CRSF, dest=b src=b -> bind mode
        // 2. CRSF bind command
        if ((package[3] == 'b' && package[4] == 'd') ||
            (header->frame_size >= 6 // official CRSF is 7 bytes with two CRCs
            && header->dest_addr == CRSF_ADDRESS_CRSF_RECEIVER
            && header->orig_addr == CRSF_ADDRESS_FLIGHT_CONTROLLER
            && header->payload[0] == CRSF_COMMAND_SUBCMD_RX
            && header->payload[1] == CRSF_COMMAND_SUBCMD_RX_BIND))
        {
            telemetry.callEnterBind = true;
            return true;
        }
        // Non CRSF, dest=b src=m -> set modelmatch
        if (package[3] == 'm' && package[4] == 'm')
        {
            telemetry.callUpdateModelMatch = true;
            telemetry.modelMatchId = package[5];
            return true;
        }
    }

    if (header->type == CRSF_FRAMETYPE_DEVICE_PING && header->dest_addr == CRSF_ADDRESS_CRSF_RECEIVER)
    {
        telemetry.sendDeviceFrame = true;
        return true;
    }

    return false;
}

static void Telemetry_AppendTelemetryPackage(uint8_t *package)
{
    const crsf_header_t *header = (crsf_header_t *) package;
    if (header->type == CRSF_FRAMETYPE_HEARTBEAT || Telemetry_processInternalTelemetryPackage(package))
    {
        return;
    }

    if (header->type >= CRSF_FRAMETYPE_DEVICE_PING)
    {
        const crsf_ext_header_t *extHeader = (crsf_ext_header_t *) package;
        if (extHeader->orig_addr == CRSF_ADDRESS_FLIGHT_CONTROLLER)
        {
#if defined(USE_MSP_WIFI)
            // this probably needs refactoring in the future, I think we should have this telemetry class inside the crsf module
            if (header->type == CRSF_FRAMETYPE_MSP_RESP || header->type == CRSF_FRAMETYPE_MSP_REQ) // if we have a client we probs wanna talk to it
            {
                wifi2tcp.crsfMspIn(package);
            }
#endif
#if defined(HAS_MSP_VTX)
            else if (header->type == CRSF_FRAMETYPE_MSP_RESP)
            {
                mspVtxProcessPacket(package);
            }
#endif
        }
    }

    const uint8_t messageSize = CRSF_FRAME_SIZE(package[CRSF_TELEMETRY_LENGTH_INDEX]);
    action_e action = ACTION_APPEND;
    comparator_t comparator = NULL;

    // Find the comparator for this message type (if any)
    for (size_t i = 0 ; i < ARRAY_SIZE(comparators) ; i++)
    {
        if (comparators[i].type == header->type)
        {
            comparator = comparators[i].comparator;
            break;
        }
    }

    // If we have a comparator or this is a 'broadcast' message we will look for a matching message in the queue and default to overwrite if we find one
    uint16_t overwritePosition = 0;
    if (comparator != NULL || header->type < CRSF_FRAMETYPE_DEVICE_PING)
    {
        for (uint16_t i = 0; i < size(&telemetry.messagePayloads);)
        {
            const uint8_t size = get(&telemetry.messagePayloads, i);
            // If the message at this point in the queue is not deleted, and it matches this comparator, then we check it
            if (!IS_DEL(size) && get(&telemetry.messagePayloads, i + 1 + CRSF_TELEMETRY_TYPE_INDEX) == header->type)
            {
                const action_e compareAction = comparator == NULL ? ACTION_OVERWRITE : comparator(header, &telemetry.messagePayloads, i + 1);
                if (compareAction != ACTION_NEXT)
                {
                    overwritePosition = i;
                    action = compareAction;
                    break;
                }
            }
            i += 1 + SIZE(size);
        }
    }
    if (isPrioritised(header->type))
    {
        telemetry.prioritizedCount++;
    }

    switch (action)
    {
    case ACTION_IGNORE:
        break;
    case ACTION_OVERWRITE:
        // Check again because our initial check was performed without locking
        if (!IS_DEL(get(&telemetry.messagePayloads, overwritePosition)))
        {
            if (get(&telemetry.messagePayloads, overwritePosition) >= messageSize)
            {
                for (uint16_t i = 0 ; i<messageSize; i++)
                {
                    set(&telemetry.messagePayloads, overwritePosition + i + 1, package[i]);
                }
                break;
            }
            // Mark the current queued entry as deleted
            set(&telemetry.messagePayloads, overwritePosition, SET_DEL(get(&telemetry.messagePayloads, overwritePosition)));
        }
        // fallthrough to APPEND
    default:
        // If there's NOT enough room on the FIFO for this message, pop until there is
        while (!available(&telemetry.messagePayloads, messageSize + 1))
        {
            const uint8_t sz = SIZE(pop(&telemetry.messagePayloads));
            skip(&telemetry.messagePayloads, sz);
        }
        push(&telemetry.messagePayloads, messageSize);
        pushBytes(&telemetry.messagePayloads, package, messageSize);
        break;
    }

}

static bool Telemetry_GetNextPayload(uint8_t* nextPayloadSize, uint8_t *currentPayload)
{
    if (telemetry.prioritizedCount)
    {
        // handle prioritised messages first
        for (uint16_t i = 0; i < size(&telemetry.messagePayloads);)
        {
            const uint8_t size = get(&telemetry.messagePayloads, i);
            // If the message at this point in the queue is not deleted, and it's a SETTINGS_ENTRY then we're going to return it
            if (isPrioritised((crsf_frame_type_e)get(&telemetry.messagePayloads, i + 1 + CRSF_TELEMETRY_TYPE_INDEX)))
            {
                if (!IS_DEL(size))
                {
                    telemetry.prioritizedCount--;
                    // If this is the first item in the queue, use pop() instead to free the space
                    if (i == 0)
                    {
                        pop(&telemetry.messagePayloads);
                        popBytes(&telemetry.messagePayloads, currentPayload, size);
                    }
                    else // Copy the frame to the current payload
                    {
                        for (uint16_t pos = 0 ; pos < size ; pos++)
                        {
                            currentPayload[pos] = get(&telemetry.messagePayloads, i + 1 + pos);
                        }
                        // Mark the current queued entry as deleted
                        set(&telemetry.messagePayloads, i, SET_DEL(size));
                    }
                    // set the pointers to the payload
                    *nextPayloadSize = CRSF_FRAME_SIZE(currentPayload[CRSF_TELEMETRY_LENGTH_INDEX]);
                    return true;
                }
            }
            i += 1 + SIZE(size);
        }
        // Didn't find one, so we'll reset the counter
        telemetry.prioritizedCount = 0;
    }

    // return the 'head' of the queue
    while (size(&telemetry.messagePayloads) > 0)
    {
        const uint8_t size = pop(&telemetry.messagePayloads);
        if (IS_DEL(size))
        {
            // This message is deleted, skip it
            skip(&telemetry.messagePayloads, SIZE(size));
            continue;
        }
        popBytes(&telemetry.messagePayloads, currentPayload, size);
        *nextPayloadSize = CRSF_FRAME_SIZE(currentPayload[CRSF_TELEMETRY_LENGTH_INDEX]);
        return true;
    }

    return false;
}

static uint8_t Telemetry_GetUpdatedModelMatch(void) 
{ 
    return telemetry.modelMatchId; 
}

static bool Telemetry_GetCrsfBatterySensorDetected(void) 
{ 
    return telemetry.crsfBatterySensorDetected; 
}

void Telemetry_Init(Telemetry_t *telemetry)
{
    telemetry->ResetState = Telemetry_ResetState;
    telemetry->RXhandleUARTin = Telemetry_RXhandleUARTin;
    telemetry->AppendTelemetryPackage = Telemetry_AppendTelemetryPackage;
    telemetry->SetCrsfBatterySensorDetected = Telemetry_SetCrsfBatterySensorDetected;
    telemetry->SetCrsfBaroSensorDetected = Telemetry_SetCrsfBaroSensorDetected;
    telemetry->ShouldCallBootloader = Telemetry_ShouldCallBootloader;
    telemetry->ShouldCallEnterBind = Telemetry_ShouldCallEnterBind;
    telemetry->ShouldCallUpdateModelMatch = Telemetry_ShouldCallUpdateModelMatch;
    telemetry->ShouldSendDeviceFrame = Telemetry_ShouldSendDeviceFrame;
    telemetry->GetNextPayload = Telemetry_GetNextPayload;
    telemetry->GetUpdatedModelMatch = Telemetry_GetUpdatedModelMatch;
    telemetry->GetCrsfBatterySensorDetected = Telemetry_GetCrsfBatterySensorDetected;
}
