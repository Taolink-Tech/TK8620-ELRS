#include <stdbool.h>
#include <string.h>

#include "rxtx_devLua.h"
#include "options.h"

extern RxConfig_t rxConfig;

static char modelString[] = "000";

static struct luaItem_string luaModelNumber = {
    {"Model Id", CRSF_INFO},
    modelString
};

static struct luaItem_string luaELRSversion = {
    {"Version", CRSF_INFO},
    firmware_menu_version
};

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
