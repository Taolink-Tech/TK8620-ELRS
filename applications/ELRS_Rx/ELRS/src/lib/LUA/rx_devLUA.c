#include <stdbool.h>
#include <string.h>

#include "rxtx_devLua.h"
#include "options.h"

extern RxConfig_t rxConfig;
void ApplyRxSerialProtocol(eSerialProtocol_e protocol) __attribute__((weak));
void RequestRxReboot(void) __attribute__((weak));

static char modelString[] = "000";

static struct luaItem_string luaModelNumber = {
    {"Model Id", CRSF_INFO},
    modelString
};

static struct luaItem_string luaELRSversion = {
    {"Version", CRSF_INFO},
    firmware_menu_version
};

static luaItem_selection_t luaSerialProtocol = {
    {"Serial Protocol", CRSF_TEXT_SELECTION},
    0,
    "CRSF;SBUS",
    ""
};

static struct luaItem_command luaReboot = {
    {"Reboot", CRSF_COMMAND},
    lcsIdle,
    ""
};

static void rebootCommand(luaPropertiesCommon_t *item, uint8_t arg)
{
    if (arg != lcsClick && arg != lcsConfirmed)
    {
        return;
    }

    sendLuaCommandResponse((struct luaItem_command *)item, lcsExecuting, "Rebooting...");
    if (RequestRxReboot)
    {
        RequestRxReboot();
    }
}

static void serialProtocolChanged(luaPropertiesCommon_t *item, uint8_t arg)
{
    (void)item;
    if (arg > 1U)
    {
        return;
    }

    eSerialProtocol_e protocol = (arg == 0U) ? PROTOCOL_CRSF : PROTOCOL_SBUS;
    setLuaTextSelectionValue(&luaSerialProtocol, arg);
    if (ApplyRxSerialProtocol)
    {
        ApplyRxSerialProtocol(protocol);
    }
}

static char *itoa_dec(uint8_t value, char *str)
{
    char tmp[3];
    uint8_t pos = 0;

    do
    {
        tmp[pos++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0U);

    for (uint8_t i = 0; i < pos; i++)
    {
        str[i] = tmp[pos - 1U - i];
    }
    str[pos] = '\0';
    return str;
}

static void registerLuaParameters(void)
{
    registerLUAParameter(&luaModelNumber, NULL, 0);
    registerLUAParameter(&luaELRSversion, NULL, 0);
    registerLUAParameter(&luaSerialProtocol, serialProtocolChanged, 0);
    registerLUAParameter(&luaReboot, rebootCommand, 0);
}

static int event(void)
{
    uint8_t modelId = rxConfig.GetModelId();
    if (modelId == 255U)
    {
        setLuaStringValue(&luaModelNumber, "Off");
    }
    else
    {
        setLuaStringValue(&luaModelNumber, itoa_dec(modelId, modelString));
    }

    setLuaStringValue(&luaELRSversion, firmware_menu_version);
    setLuaTextSelectionValue(
        &luaSerialProtocol,
        rxConfig.GetSerialProtocol() == PROTOCOL_SBUS ? 1U : 0U
    );
    return DURATION_IMMEDIATELY;
}

static int timeout(void)
{
    luaHandleUpdateParameter();
    return (connectionState == connected) ? ExpressLRS_currAirRate_Modparams->interval / 250 : 1000;
}

static int start(void)
{
    luaResetParameters();
    registerLuaParameters();
    event();
    return DURATION_IMMEDIATELY;
}

device_t LUA_device = {
    .initialize = NULL,
    .start = start,
    .event = event,
    .timeout = timeout
};
