#include <stdbool.h>
#include "options.h"
#include "logging.h"
#include "version.h"

const char version[] = TK8620_ELRS_VERSION_STRING;
const char firmware_commit[] = TK8620_ELRS_GIT_COMMIT;
const char firmware_menu_version[] = TK8620_ELRS_MENU_VERSION;
const char firmware_build_id[] = TK8620_ELRS_BUILD_ID;
const char firmware_upstream[] = "ExpressLRS " ELRS_UPSTREAM_VERSION " " ELRS_UPSTREAM_COMMIT;
#ifndef DEVICE_NAME
#define DEVICE_NAME "ELRS 900RX"
#endif
const char device_name[] = DEVICE_NAME;

__attribute__ ((used)) static firmware_options_t flashedOptions = {
    ._magic_ = {0xBE, 0xEF, 0xBA, 0xBE, 0xCA, 0xFE, 0xF0, 0x0D},
    ._version_ = 3,
    .domain = 0,

#if defined(MY_UID)
    .hasUID = true,
    .uid = { MY_UID },
#else
    .hasUID = false,
    .uid = {},
#endif
#if defined(FLASH_DISCRIM)
    .flash_discriminator = FLASH_DISCRIM,
#else
    .flash_discriminator = 0,
#endif
#if defined(FAN_MIN_RUNTIME)
    .fan_min_runtime = FAN_MIN_RUNTIME,
#else
    .fan_min_runtime = 30,
#endif
#if defined(PLATFORM_ESP32) || defined(PLATFORM_ESP8266)
    #if defined(AUTO_WIFI_ON_INTERVAL)
        .wifi_auto_on_interval = AUTO_WIFI_ON_INTERVAL * 1000,
    #else
        .wifi_auto_on_interval = -1,
    #endif
    #if defined(HOME_WIFI_SSID)
        .home_wifi_ssid = {HOME_WIFI_SSID},
    #else
        .home_wifi_ssid = {},
    #endif
    #if defined(HOME_WIFI_PASSWORD)
        .home_wifi_password = {HOME_WIFI_PASSWORD},
    #else
        .home_wifi_password = {},
    #endif
#endif
#if defined(USE_SBUS_PROTOCOL)
    .uart_baud = 100000,
#elif defined(RCVR_UART_BAUD)
    .uart_baud = RCVR_UART_BAUD,
#else
    .uart_baud = 420000,
#endif
#if defined(LOCK_ON_FIRST_CONNECTION)
    .lock_on_first_connection = true,
#else
    .lock_on_first_connection = false,
#endif
#if defined(TLM_REPORT_INTERVAL_MS)
    .tlm_report_interval = TLM_REPORT_INTERVAL_MS,
#else
    .tlm_report_interval = 240U,
#endif
#if defined(UNLOCK_HIGHER_POWER)
    .unlock_higher_power = true,
#else
    .unlock_higher_power = false,
#endif
#if defined(GPIO_PIN_BUZZER)
    #if defined(DISABLE_ALL_BEEPS)
    .buzzer_mode = buzzerQuiet,
    .buzzer_melody = {},
    #elif defined(JUST_BEEP_ONCE)
    .buzzer_mode = buzzerOne,
    .buzzer_melody = {},
    #elif defined(DISABLE_STARTUP_BEEP)
    .buzzer_mode = buzzerTune,
    .buzzer_melody = {{400, 200}, {480, 200}},
    #elif defined(MY_STARTUP_MELODY)
    .buzzer_mode = buzzerTune,
    .buzzer_melody = MY_STARTUP_MELODY_ARR,
    #else
    .buzzer_mode = buzzerTune,
    .buzzer_melody = {{659, 300}, {659, 300}, {523, 100}, {659, 300}, {783, 550}, {392, 575}},
    #endif
#endif
};

/*
 * This all seems rather convoluted, but it means that the compiler/linker optimisations
 * don't create multiple copies of the UID. This code forces the firmwareOptions to be copied
 * into RAM and all the other areas of code are forced to use the RAM copy.
 */
firmware_options_t firmwareOptions;
bool options_init()
{
    firmwareOptions = flashedOptions;
    return true;
}

