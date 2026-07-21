#pragma once

// #include "targets.h"
#include <stdbool.h>
#include "crsf_protocol.h"

typedef enum {
    //bit 0 and 1 are status flags, show up as the little icon in the lua top right corner
    LUA_FLAG_CONNECTED = 0,
    LUA_FLAG_STATUS1,
    //bit 2,3,4 are warning flags, change the tittle bar every 0.5s
    LUA_FLAG_MODEL_MATCH,
    LUA_FLAG_ISARMED,
    LUA_FLAG_WARNING1,
    //bit 5,6,7 are critical warning flag, block the lua screen until user confirm to suppress the warning.
    LUA_FLAG_ERROR_CONNECTED,
    LUA_FLAG_ERROR_BAUDRATE,
    LUA_FLAG_CRITICAL_WARNING2,
} lua_Flags_e;

typedef struct {
    const char* name;   // display name
    crsf_value_type_e type;
    uint8_t id;         // Sequential id assigned by enumeration
    uint8_t parent;     // id of parent folder
} PACKED luaPropertiesCommon_t;

struct tagLuaDeviceProperties {
    uint32_t serialNo;
    uint32_t hardwareVer;
    uint32_t softwareVer;
    uint8_t fieldCnt; //number of field of params this device has
}PACKED;

typedef struct {
    luaPropertiesCommon_t common;
    uint8_t value;
    const char* options; // selection options, separated by ';'
    const char* units;
} PACKED luaItem_selection_t;

typedef enum {
    lcsIdle = 0,
    lcsClick = 1,       // user has clicked the command to execute
    lcsExecuting = 2,   // command is executing
    lcsAskConfirm = 3,  // command pending user OK
    lcsConfirmed = 4,   // user has confirmed
    lcsCancel = 5,      // user has requested cancel
    lcsQuery = 6,       // UI is requesting status update
} luaCmdStep_e;

struct luaItem_command {
    luaPropertiesCommon_t common;
    luaCmdStep_e step;      // state
    const char *info;       // status info to display
} PACKED;

struct luaPropertiesInt8 {
    union {
        struct {
            uint8_t value;
            const uint8_t min;
            const uint8_t max;
        } u;
        struct {
            int8_t value;
            const int8_t min;
            const int8_t max;
        } s;
    };
} PACKED;

struct luaItem_int8 {
    luaPropertiesCommon_t common;
    struct luaPropertiesInt8 properties;
    const char* const units;
} PACKED;

struct luaPropertiesInt16 {
    union {
        struct {
            uint16_t value;
            const uint16_t min;
            const uint16_t max;
        } u;
        struct {
            int16_t value;
            const int16_t min;
            const int16_t max;
        } s;
    };
} PACKED;

struct luaItem_int16 {
    luaPropertiesCommon_t common;
    struct luaPropertiesInt16 properties;
    const char* const units;
} PACKED;

struct luaPropertiesFloat {
    // value, min, max, and def are all signed, but stored as BE unsigned
    uint32_t value;
    const uint32_t min;
    const uint32_t max;
    const uint32_t def; // default value
    const uint8_t precision;
    const uint32_t step;
} PACKED;

struct luaItem_float {
    luaPropertiesCommon_t common;
    struct luaPropertiesFloat properties;
    const char* const units;
} PACKED;

struct luaItem_string {
    luaPropertiesCommon_t common;
    const char* value;
} PACKED;

typedef struct {
    luaPropertiesCommon_t common;
    char* dyn_name;
} PACKED luaItem_folder_t;

typedef struct {
    uint8_t pktsBad;
    uint16_t pktsGood; // Big-Endian
    uint8_t flags;
    char msg[1]; // null-terminated string
} PACKED tagLuaElrsParams_t;

void setLuaWarningFlag(lua_Flags_e flag, bool value);
uint8_t getLuaWarningFlags(void);

void luaRegisterDevicePingCallback(void (*callback)());

#define LUA_FIELD_HIDE(fld) { fld.common.type = (crsf_value_type_e)((uint8_t)fld.common.type | CRSF_FIELD_HIDDEN); }
#define LUA_FIELD_SHOW(fld) { fld.common.type = (crsf_value_type_e)((uint8_t)fld.common.type & ~CRSF_FIELD_HIDDEN); }
#define LUA_FIELD_VISIBLE(fld, cond) { if (cond) LUA_FIELD_SHOW(fld) else LUA_FIELD_HIDE(fld) }

void sendLuaCommandResponse(struct luaItem_command *cmd, luaCmdStep_e step, const char *message);

void luaResetParameters(void);
uint8_t luaGetFieldCount(void);

extern void luaParamUpdateReq(uint8_t type, uint8_t index, uint8_t arg);
extern bool luaParamUpdatePending(void);
extern bool luaHandleUpdateParameter(void);

typedef void (*luaCallback)(luaPropertiesCommon_t *item, uint8_t arg);
void registerLUAParameter(void *definition, luaCallback callback, uint8_t parent);

uint8_t findLuaSelectionLabel(const void *luaStruct, char *outarray, uint8_t value);

inline void setLuaTextSelectionValue(luaItem_selection_t *luaStruct, uint8_t newvalue) {
    luaStruct->value = newvalue;
}
inline void setLuaUint8Value(struct luaItem_int8 *luaStruct, uint8_t newvalue) {
    luaStruct->properties.u.value = newvalue;
}
inline void setLuaInt8Value(struct luaItem_int8 *luaStruct, int8_t newvalue) {
    luaStruct->properties.s.value = newvalue;
}
// inline void setLuaUint16Value(struct luaItem_int16 *luaStruct, uint16_t newvalue) {
//     luaStruct->properties.u.value = htobe16(newvalue);
// }
// inline void setLuaInt16Value(struct luaItem_int16 *luaStruct, int16_t newvalue) {
//     luaStruct->properties.u.value = htobe16((uint16_t)newvalue);
// }
// inline void setLuaFloatValue(struct luaItem_float *luaStruct, int32_t newvalue) {
//     luaStruct->properties.value = htobe32((uint32_t)newvalue);
// }
inline void setLuaStringValue(struct luaItem_string *luaStruct, const char *newvalue) {
    luaStruct->value = newvalue;
}

#define LUASYM_ARROW_UP "\xc0"
#define LUASYM_ARROW_DN "\xc1"
