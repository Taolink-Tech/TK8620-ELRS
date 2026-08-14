#include "unified_config.h"

#if ELRS_UNIFIED

#include <stddef.h>
#include <string.h>

#include "flash_hal.h"
#include "tk86xx_api.h"

#define UNIFIED_MODE_FLASH_OFFSET       0x2000U
#define UNIFIED_MODE_RECORD_MAGIC       0x444D4C45UL /* ELMD */
#define UNIFIED_MODE_RECORD_VERSION     1U
#define UNIFIED_CONFIG_PROTOCOL_VERSION 1U
#define UNIFIED_CONFIG_MAX_PAYLOAD      20U
#define UNIFIED_CONFIG_SESSION_MS       60000U
#define UNIFIED_CONFIG_SWITCH_DELAY_MS  30U
#define UNIFIED_CONFIG_REBOOT_DELAY_MS  50U
#define UNIFIED_CONFIG_BAUD             115200U

enum {
    CFG_CMD_HELLO = 1,
    CFG_CMD_HELLO_ACK = 2,
    CFG_CMD_GET_INFO = 3,
    CFG_CMD_GET_CONFIG = 4,
    CFG_CMD_SET_MODE = 5,
    CFG_CMD_REBOOT = 6,
    CFG_CMD_ERROR = 0x7f,
};

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint8_t role;
    uint8_t mode;
    uint8_t reserved[18];
    uint32_t crc32;
} unified_mode_record_t;

_Static_assert(sizeof(unified_mode_record_t) == 32U, "unified mode record must be 32 bytes");

static const uint8_t s_frameMagic[8] = {'T', 'K', 'E', 'L', 'R', 'S', 'C', 'F'};

typedef enum {
    CFG_PHASE_DISCOVERY = 0,
    CFG_PHASE_SWITCH_PENDING,
    CFG_PHASE_SESSION,
} config_phase_e;

static unified_role_e s_role;
static unified_mode_e s_mode = UNIFIED_MODE_RC;
static bool s_recordValid;
static const char *s_version;
static const char *s_buildId;
static UnifiedConfigLifecycleFn s_stopNormalOperation;
static UnifiedConfigLifecycleFn s_requestReboot;
static volatile config_phase_e s_phase;
static volatile bool s_framePending;
static uint8_t s_pendingFrame[20U + UNIFIED_CONFIG_MAX_PAYLOAD];
static uint8_t s_pendingLength;
static uint8_t s_filterBuffer[20U + UNIFIED_CONFIG_MAX_PAYLOAD];
static uint8_t s_filterLength;
static uint8_t s_filterExpected;
static bool s_discoveryEnabled;
static bool s_bootProbeActive;
static bool s_loggingEnabled;
static uint32_t s_sessionDeadline;
static uint32_t s_switchAt;
static uint32_t s_rebootAt;
static uint32_t s_sessionChallenge;
static bool s_rebootPending;

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
    crc = ~crc;
    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8U; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)-(int32_t)(crc & 1U));
        }
    }
    return ~crc;
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void write_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static bool record_is_valid(const unified_mode_record_t *record)
{
    if (record->magic != UNIFIED_MODE_RECORD_MAGIC ||
        record->version != UNIFIED_MODE_RECORD_VERSION ||
        record->length != sizeof(*record) ||
        record->role != (uint8_t)s_role ||
        record->mode > (uint8_t)UNIFIED_MODE_AIRPORT) {
        return false;
    }

    return crc32_update(0U, (const uint8_t *)record, offsetof(unified_mode_record_t, crc32)) == record->crc32;
}

static void load_record(void)
{
    unified_mode_record_t record;
    memset(&record, 0, sizeof(record));
    s_recordValid = flash_user_read(UNIFIED_MODE_FLASH_OFFSET, (uint8_t *)&record, sizeof(record)) >= 0 &&
                    record_is_valid(&record);
    s_mode = s_recordValid ? (unified_mode_e)record.mode : UNIFIED_MODE_RC;
}

static bool store_mode(unified_mode_e mode)
{
    unified_mode_record_t record;
    unified_mode_record_t verify;

    memset(&record, 0, sizeof(record));
    record.magic = UNIFIED_MODE_RECORD_MAGIC;
    record.version = UNIFIED_MODE_RECORD_VERSION;
    record.length = sizeof(record);
    record.role = (uint8_t)s_role;
    record.mode = (uint8_t)mode;
    record.crc32 = crc32_update(0U, (const uint8_t *)&record, offsetof(unified_mode_record_t, crc32));

    if (flash_user_erase(UNIFIED_MODE_FLASH_OFFSET, sizeof(record)) < 0 ||
        flash_user_write(UNIFIED_MODE_FLASH_OFFSET, (uint8_t *)&record, sizeof(record)) < 0 ||
        flash_user_read(UNIFIED_MODE_FLASH_OFFSET, (uint8_t *)&verify, sizeof(verify)) < 0 ||
        memcmp(&record, &verify, sizeof(record)) != 0 ||
        !record_is_valid(&verify)) {
        s_recordValid = false;
        s_mode = UNIFIED_MODE_RC;
        return false;
    }

    s_recordValid = true;
    s_mode = mode;
    return true;
}

static void serial_init_config(void)
{
    const Tk86xxSerialConfig config = {
        .baudRate = UNIFIED_CONFIG_BAUD,
        .wordLength = TK86XX_SERIAL_WORD_LENGTH_8B,
        .parity = TK86XX_SERIAL_PARITY_NONE,
        .stopBits = TK86XX_SERIAL_STOP_BITS_1,
        .duplex = TK86XX_SERIAL_DUPLEX_FULL,
    };
    (void)Tk86xxSerialInit(&config);
}

static uint8_t frame_build(uint8_t *out, uint8_t command, uint8_t sequence,
                           uint32_t challenge, const uint8_t *payload, uint8_t payloadLength)
{
    uint8_t length = (uint8_t)(20U + payloadLength);
    memcpy(out, s_frameMagic, sizeof(s_frameMagic));
    out[8] = UNIFIED_CONFIG_PROTOCOL_VERSION;
    out[9] = command;
    out[10] = sequence;
    out[11] = payloadLength;
    write_u32_le(&out[12], challenge);
    if (payloadLength != 0U) {
        memcpy(&out[16], payload, payloadLength);
    }
    write_u32_le(&out[16U + payloadLength], crc32_update(0U, out, (uint32_t)(16U + payloadLength)));
    return length;
}

static bool frame_send(uint8_t command, uint8_t sequence, uint32_t challenge,
                       const uint8_t *payload, uint8_t payloadLength)
{
    uint8_t frame[20U + UNIFIED_CONFIG_MAX_PAYLOAD];
    uint8_t length = frame_build(frame, command, sequence, challenge, payload, payloadLength);
    APIRet result = Tk86xxSerialWrite(frame, length);
    /* API_TIMEOUT is returned when the final TX-idle wait expires after the
       complete frame has already been loaded into the UART FIFO. */
    return result == API_SUCCESS || result == API_TIMEOUT;
}

static bool frame_is_valid(const uint8_t *frame, uint8_t length)
{
    if (length < 20U || memcmp(frame, s_frameMagic, sizeof(s_frameMagic)) != 0 ||
        frame[8] != UNIFIED_CONFIG_PROTOCOL_VERSION ||
        frame[11] > UNIFIED_CONFIG_MAX_PAYLOAD ||
        length != (uint8_t)(20U + frame[11])) {
        return false;
    }
    return read_u32_le(&frame[length - 4U]) == crc32_update(0U, frame, length - 4U);
}

static void filter_flush(UnifiedConfigForwardByteFn forwardByte)
{
    if (forwardByte != NULL) {
        for (uint8_t i = 0; i < s_filterLength; i++) {
            forwardByte(s_filterBuffer[i]);
        }
    }
    s_filterLength = 0U;
    s_filterExpected = 0U;
}

void UnifiedConfig_FilterBytes(const uint8_t *data, uint16_t length,
                               UnifiedConfigForwardByteFn forwardByte)
{
    if (s_phase == CFG_PHASE_DISCOVERY && !s_discoveryEnabled) {
        if (forwardByte != NULL) {
            for (uint16_t i = 0; i < length; i++) {
                forwardByte(data[i]);
            }
        }
        return;
    }

    for (uint16_t i = 0; i < length; i++) {
        uint8_t byte = data[i];

        if (s_filterLength < sizeof(s_frameMagic)) {
            if (byte != s_frameMagic[s_filterLength]) {
                filter_flush(forwardByte);
                if (byte == s_frameMagic[0]) {
                    s_filterBuffer[0] = byte;
                    s_filterLength = 1U;
                } else if (forwardByte != NULL) {
                    forwardByte(byte);
                }
                continue;
            }
        }

        s_filterBuffer[s_filterLength++] = byte;
        if (s_filterLength == 12U) {
            if (s_filterBuffer[11] > UNIFIED_CONFIG_MAX_PAYLOAD) {
                filter_flush(forwardByte);
                continue;
            }
            s_filterExpected = (uint8_t)(20U + s_filterBuffer[11]);
        }

        if (s_filterExpected != 0U && s_filterLength == s_filterExpected) {
            uint8_t command = s_filterBuffer[9];
            bool allowed = (s_phase == CFG_PHASE_SESSION) ||
                           (s_phase == CFG_PHASE_DISCOVERY && command == CFG_CMD_HELLO);
            if (allowed && !s_framePending && frame_is_valid(s_filterBuffer, s_filterLength)) {
                memcpy(s_pendingFrame, s_filterBuffer, s_filterLength);
                s_pendingLength = s_filterLength;
                s_framePending = true;
                s_filterLength = 0U;
                s_filterExpected = 0U;
            } else {
                filter_flush(forwardByte);
            }
        }
    }
}

static void session_rx_callback(uint8_t *data, uint8_t length)
{
    UnifiedConfig_FilterBytes(data, length, NULL);
}

static uint8_t copy_string(uint8_t *out, uint8_t capacity, const char *value)
{
    uint8_t length = 0U;
    if (value == NULL) {
        return 0U;
    }
    while (length < capacity && value[length] != '\0') {
        out[length] = (uint8_t)value[length];
        length++;
    }
    return length;
}

static void handle_session_frame(const uint8_t *frame, uint8_t length, uint32_t nowMs)
{
    (void)length;
    uint8_t command = frame[9];
    uint8_t sequence = frame[10];
    uint8_t payloadLength = frame[11];
    uint32_t challenge = read_u32_le(&frame[12]);
    const uint8_t *payload = &frame[16];
    uint8_t response[UNIFIED_CONFIG_MAX_PAYLOAD];
    uint8_t responseLength = 0U;

    s_sessionDeadline = nowMs + UNIFIED_CONFIG_SESSION_MS;

    switch (command) {
    case CFG_CMD_GET_INFO: {
        uint8_t versionLength;
        uint8_t buildLength;
        response[responseLength++] = (uint8_t)s_role;
        response[responseLength++] = (uint8_t)s_mode;
        response[responseLength++] = s_recordValid ? 1U : 0U;
        response[responseLength++] = UNIFIED_CONFIG_PROTOCOL_VERSION;
        versionLength = copy_string(&response[responseLength + 1U],
                                    (uint8_t)(UNIFIED_CONFIG_MAX_PAYLOAD - 6U), s_version);
        response[responseLength] = versionLength;
        responseLength = (uint8_t)(responseLength + 1U + versionLength);
        buildLength = copy_string(&response[responseLength + 1U],
                                  (uint8_t)(UNIFIED_CONFIG_MAX_PAYLOAD - responseLength - 1U), s_buildId);
        response[responseLength] = buildLength;
        responseLength = (uint8_t)(responseLength + 1U + buildLength);
        frame_send(CFG_CMD_GET_INFO, sequence, challenge, response, responseLength);
        break;
    }
    case CFG_CMD_GET_CONFIG:
        response[0] = s_recordValid ? 1U : 0U;
        response[1] = (uint8_t)s_role;
        response[2] = (uint8_t)s_mode;
        response[3] = UNIFIED_MODE_RECORD_VERSION;
        frame_send(CFG_CMD_GET_CONFIG, sequence, challenge, response, 4U);
        break;
    case CFG_CMD_SET_MODE:
        if (payloadLength != 1U || payload[0] > (uint8_t)UNIFIED_MODE_AIRPORT) {
            response[0] = 1U;
        } else if (s_recordValid && payload[0] == (uint8_t)s_mode) {
            response[0] = 0U;
        } else {
            response[0] = store_mode((unified_mode_e)payload[0]) ? 0U : 2U;
        }
        response[1] = (uint8_t)s_mode;
        response[2] = s_recordValid ? 1U : 0U;
        frame_send(CFG_CMD_SET_MODE, sequence, challenge, response, 3U);
        break;
    case CFG_CMD_REBOOT:
        response[0] = 0U;
        if (frame_send(CFG_CMD_REBOOT, sequence, challenge, response, 1U)) {
            s_rebootAt = nowMs + UNIFIED_CONFIG_REBOOT_DELAY_MS;
            s_rebootPending = true;
        }
        break;
    default:
        response[0] = command;
        frame_send(CFG_CMD_ERROR, sequence, challenge, response, 1U);
        break;
    }
}

void UnifiedConfig_Init(unified_role_e role,
                        const char *version,
                        const char *buildId,
                        UnifiedConfigLifecycleFn stopNormalOperation,
                        UnifiedConfigLifecycleFn requestReboot)
{
    s_role = role;
    s_version = version;
    s_buildId = buildId;
    s_stopNormalOperation = stopNormalOperation;
    s_requestReboot = requestReboot;
    s_phase = CFG_PHASE_DISCOVERY;
    s_framePending = false;
    s_filterLength = 0U;
    s_filterExpected = 0U;
    s_rebootPending = false;
    s_discoveryEnabled = false;
    s_bootProbeActive = false;
    s_loggingEnabled = false;
    load_record();
}

bool UnifiedConfig_IsAirport(void) { return s_mode == UNIFIED_MODE_AIRPORT; }
bool UnifiedConfig_IsSessionActive(void) { return s_phase != CFG_PHASE_DISCOVERY; }
bool UnifiedConfig_IsRecordValid(void) { return s_recordValid; }
unified_mode_e UnifiedConfig_GetStoredMode(void) { return s_mode; }
void UnifiedConfig_SetLoggingEnabled(bool enabled) { s_loggingEnabled = enabled; }
bool UnifiedConfig_IsLoggingEnabled(void)
{
    return s_loggingEnabled && s_mode == UNIFIED_MODE_RC && s_phase == CFG_PHASE_DISCOVERY;
}

void UnifiedConfig_StartBootProbe(void)
{
    s_bootProbeActive = true;
    s_discoveryEnabled = true;
    serial_init_config();
    Tk86xxSerialRegisterRxCallback(session_rx_callback);
}

void UnifiedConfig_EndBootProbe(void)
{
    s_bootProbeActive = false;
    /* Configuration discovery is startup-only. Once normal UART traffic is
       enabled, bypass the management parser for byte-for-byte transparency. */
    s_discoveryEnabled = false;
    s_filterLength = 0U;
    s_filterExpected = 0U;
}

void UnifiedConfig_Update(uint32_t nowMs)
{
    if (s_framePending) {
        uint8_t frame[sizeof(s_pendingFrame)];
        uint8_t length = s_pendingLength;
        memcpy(frame, s_pendingFrame, length);
        s_framePending = false;

        if (s_phase == CFG_PHASE_DISCOVERY && frame[9] == CFG_CMD_HELLO) {
            uint8_t payload[4] = {(uint8_t)s_role, (uint8_t)s_mode, s_recordValid ? 1U : 0U,
                                  UNIFIED_CONFIG_PROTOCOL_VERSION};
            bool loggingWasEnabled = UnifiedConfig_IsLoggingEnabled();
            UnifiedConfig_SetLoggingEnabled(false);
            bool ackWriteQueued = frame_send(CFG_CMD_HELLO_ACK, frame[10],
                                             read_u32_le(&frame[12]), payload, sizeof(payload));
            if (ackWriteQueued) {
                s_sessionChallenge = read_u32_le(&frame[12]);
                if (s_bootProbeActive) {
                    /* The startup probe is already at 115200 and normal RF/UART
                       operation has not been initialized yet. Enter the session
                       directly instead of stopping uninitialized subsystems. */
                    s_phase = CFG_PHASE_SESSION;
                    s_sessionDeadline = nowMs + UNIFIED_CONFIG_SESSION_MS;
                } else {
                    s_switchAt = nowMs + UNIFIED_CONFIG_SWITCH_DELAY_MS;
                    s_phase = CFG_PHASE_SWITCH_PENDING;
                    if (s_stopNormalOperation != NULL) {
                        s_stopNormalOperation();
                    }
                }
            } else if (loggingWasEnabled && !s_bootProbeActive) {
                UnifiedConfig_SetLoggingEnabled(true);
            }
        } else if (s_phase == CFG_PHASE_SESSION &&
                   read_u32_le(&frame[12]) == s_sessionChallenge) {
            handle_session_frame(frame, length, nowMs);
        }
    }

    if (s_phase == CFG_PHASE_SWITCH_PENDING && (int32_t)(nowMs - s_switchAt) >= 0) {
        serial_init_config();
        Tk86xxSerialRegisterRxCallback(session_rx_callback);
        s_filterLength = 0U;
        s_filterExpected = 0U;
        s_phase = CFG_PHASE_SESSION;
        s_sessionDeadline = nowMs + UNIFIED_CONFIG_SESSION_MS;
    }

    if (s_phase == CFG_PHASE_SESSION && (int32_t)(nowMs - s_sessionDeadline) >= 0) {
        if (s_requestReboot != NULL) {
            s_requestReboot();
        }
    }

    if (s_rebootPending && (int32_t)(nowMs - s_rebootAt) >= 0) {
        s_rebootPending = false;
        if (s_requestReboot != NULL) {
            s_requestReboot();
        }
    }
}

#endif
