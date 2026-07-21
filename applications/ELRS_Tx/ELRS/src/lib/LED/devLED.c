#include "common.h"
#include "devLED.h"

#include "gpio_hal.h"
#include "helpers.h"

#define GPIO_PIN_LED GPIO_PIN_A0

#ifndef GPIO_PIN_LED_RED
#ifdef GPIO_PIN_LED
#define GPIO_PIN_LED_RED GPIO_PIN_LED
#else
#define GPIO_PIN_LED_RED UNDEF_PIN
#endif
#endif

#ifndef GPIO_PIN_LED
#define GPIO_PIN_LED GPIO_PIN_LED_RED
#endif

#ifndef GPIO_LED_RED_INVERTED
#define GPIO_LED_RED_INVERTED 1
#endif

static const uint8_t LEDSEQ_BINDING[] = { 10, 10, 10, 100 }; // 2x 100 ms blink, 1 s pause

static uint8_t s_ledPin = UNDEF_PIN;
static const uint8_t *s_ledDurations;
static uint8_t s_ledDurationCount;
static uint8_t s_ledCounter;

static void setLedLevel(bool on)
{
    if (GPIO_PIN_LED == UNDEF_PIN)
    {
        return;
    }

    gpioa_pin_write(GPIO_PIN_LED, (on ? 1 : 0) ^ GPIO_LED_RED_INVERTED);
}

static uint16_t updateBlink(void)
{
    if (s_ledPin == UNDEF_PIN)
    {
        return DURATION_NEVER;
    }

    setLedLevel((s_ledCounter % 2U) == 0U);
    if (s_ledCounter >= s_ledDurationCount)
    {
        s_ledCounter = 0;
    }
    return s_ledDurations[s_ledCounter++] * 10U;
}

static uint16_t startBlink(const uint8_t durations[], uint8_t count)
{
    s_ledCounter = 0;
    s_ledPin = GPIO_PIN_LED;
    s_ledDurations = durations;
    s_ledDurationCount = count;
    return updateBlink();
}

static void initialize(void)
{
    if (GPIO_PIN_LED == UNDEF_PIN)
    {
        return;
    }

    gpio_init_t param;
    iomux_set(GPIO_PIN_LED, IOMUX_GPIO);
    param.dir = GPIO_DIR_OUT;
    param.pin = GPIO_PIN_LED;
    param.pull = GPIO_PULL_UP;
    gpioa_init(&param);

    s_ledPin = GPIO_PIN_LED;
    setLedLevel(true);
}

static int timeout(void)
{
    return updateBlink();
}

static int event(void)
{
    if (GPIO_PIN_LED == UNDEF_PIN)
    {
        return DURATION_NEVER;
    }

    if (InBindingMode)
    {
        return startBlink(LEDSEQ_BINDING, ARRAY_SIZE(LEDSEQ_BINDING));
    }

    setLedLevel(true);
    return DURATION_NEVER;
}

device_t LED_device = {
    .initialize = initialize,
    .start = event,
    .event = event,
    .timeout = timeout
};
