#pragma once

#include <stdint.h>
#include <stdbool.h>

extern const char version[];
extern const char firmware_commit[];
extern const char firmware_menu_version[];
extern const char firmware_build_id[];
extern const char firmware_upstream[];

enum BuzzerMode {
    buzzerQuiet,
    buzzerOne,
    buzzerTune
};

typedef struct _options {
    uint8_t     _magic_[8];     // this is the magic constant so the configurator can find this options block
    uint16_t    _version_;      // the version of this structure
    uint8_t     domain;         // depends on radio chip
    uint8_t     hasUID;
    uint8_t     uid[6];         // MY_UID derived from MY_BINDING_PHRASE
    uint32_t    flash_discriminator;    // Discriminator value used to determine if the device has been reflashed and therefore
                                        // the SPIFSS settings are obsolete and the flashed settings should be used in preference
    uint32_t    fan_min_runtime;
#if defined(PLATFORM_ESP32) || defined(PLATFORM_ESP8266)
    int32_t     wifi_auto_on_interval;
    char        home_wifi_ssid[33];
    char        home_wifi_password[65];
#endif
    uint32_t    uart_baud;
    bool        lock_on_first_connection:1;
    uint32_t    tlm_report_interval;
    bool        unlock_higher_power:1;
    uint8_t     reserved_flags:7;
#if defined(GPIO_PIN_BUZZER)
    uint8_t     buzzer_mode;            // 0 = disable all, 1 = beep once, 2 = disable startup beep, 3 = default tune, 4 = custom tune
    uint16_t    buzzer_melody[32][2];
#endif
} __attribute__((packed)) firmware_options_t;


extern firmware_options_t firmwareOptions;
extern const char device_name[];
extern bool options_init();

