#include "devButton.h"

#include <stddef.h>
#include "button.h"
#include "helpers.h"
#include "handset.h"
#include "gpio_hal.h"
#include "tk86xx_platform.h"

#define GPIO_PIN_BUTTON (GPIO_PIN_A6)

#ifndef GPIO_BUTTON_INVERTED
#define GPIO_BUTTON_INVERTED true
#endif

#ifndef GPIO_BUTTON2_INVERTED
#define GPIO_BUTTON2_INVERTED false
#endif

#if !defined(GPIO_PIN_BUTTON2)
#define GPIO_PIN_BUTTON2 UNDEF_PIN
#endif

static Button_t button1;
#if defined(GPIO_PIN_BUTTON2)
static Button_t button2;
#endif

static const struct {
    bool pressType;
    uint8_t count;
    action_e action;
} button_actions[] = {
    {true, 2, ACTION_BIND},
    {true, 9, ACTION_START_WIFI},
    {true, 23, ACTION_RESET_REBOOT}
};

static ButtonAction_fn actions[ACTION_LAST] = { NULL };

void registerButtonFunction(action_e action, ButtonAction_fn function)
{
    actions[action] = function;
}

static uint8_t button_GetActionCnt(void)
{
    return ARRAY_SIZE(button_actions);
}

static void handlePress(uint8_t button, bool longPress, uint8_t count)
{
    for (unsigned i = 0; i < button_GetActionCnt(); i++)
    {
        if (button_actions[i].action != ACTION_NONE &&
            button_actions[i].pressType == longPress &&
            button_actions[i].count == count - 1)
        {
            if (actions[button_actions[i].action] != NULL)
            {
                actions[button_actions[i].action]();
            }
        }
    }
    UNUSED(button);
}

static void init(Button_t *button, uint8_t pin, bool idlelow)
{
    button->_pin = pin;
    button->_idlelow = idlelow;
    button->_lastCheck = 0;
    button->_lastFallingEdge = 0;
    button->_state = STATE_IDLE;
    button->_isLongPress = false;
    button->_longCount = 0;
    button->_pressCount = 0;

    gpio_init_t param;
    iomux_set(pin, IOMUX_GPIO);
    param.dir  = GPIO_DIR_IN;
    param.pin  = pin;
    param.pull = idlelow ? GPIO_PULL_DOWN : GPIO_PULL_UP;
    gpioa_init(&param);
}

static uint8_t getCount(Button_t *button)
{
    return button->_pressCount;
}

static uint8_t getLongCount(Button_t *button)
{
    return button->_longCount;
}

static void onShortPress_callback(void)
{
    handlePress(0, false, getCount(&button1));
}

static void onLongPress_callback(void)
{
    handlePress(0, true, getLongCount(&button1) + 1);
}

#if defined(GPIO_PIN_BUTTON2)
static void onShortPress2_callback(void)
{
    handlePress(1, false, getCount(&button2));
}

static void onLongPress2_callback(void)
{
    handlePress(1, true, getLongCount(&button2) + 1);
}
#endif

static int start(void)
{
    if (GPIO_PIN_BUTTON == UNDEF_PIN)
    {
        return DURATION_NEVER;
    }

    init(&button1, GPIO_PIN_BUTTON, GPIO_BUTTON_INVERTED);
    button1.OnShortPress = onShortPress_callback;
    button1.OnLongPress = onLongPress_callback;

#if defined(GPIO_PIN_BUTTON2)
    if (GPIO_PIN_BUTTON2 != UNDEF_PIN)
    {
        init(&button2, GPIO_PIN_BUTTON2, GPIO_BUTTON2_INVERTED);
        button2.OnShortPress = onShortPress2_callback;
        button2.OnLongPress = onLongPress2_callback;
    }
#endif

    return DURATION_IMMEDIATELY;
}

static int update(Button_t *button)
{
    const uint32_t now = millis();

    if (now - button->_lastFallingEdge > MS_MULTI_TIMEOUT)
    {
        button->_pressCount = 0;
    }

    button->_state = (button->_state << 1) & 0b110;
    button->_state |= gpioa_pin_read(button->_pin) ^ button->_idlelow;

    if (button->_state == STATE_RISE)
    {
        if (!button->_isLongPress)
        {
            ++button->_pressCount;
            if (button->OnShortPress != NULL)
            {
                button->OnShortPress();
            }
        }
        button->_isLongPress = false;
    }
    else if (button->_state == STATE_FALL)
    {
        button->_lastFallingEdge = now;
        button->_longCount = 0;
    }
    else if (button->_state == STATE_HELD)
    {
        if (now - button->_lastFallingEdge > MS_LONG)
        {
            button->_isLongPress = true;
            if (button->OnLongPress != NULL)
            {
                button->OnLongPress();
            }
            button->_lastFallingEdge = now;
            button->_longCount++;
        }
    }
    return MS_DEBOUNCE;
}

static int timeout(void)
{
    if (GPIO_PIN_BUTTON == UNDEF_PIN)
    {
        return DURATION_NEVER;
    }

#if defined(GPIO_PIN_BUTTON2)
    if (GPIO_PIN_BUTTON2 != UNDEF_PIN)
    {
        update(&button2);
    }
#endif
    return update(&button1);
}

device_t Button_device = {
    .initialize = NULL,
    .start = start,
    .event = NULL,
    .timeout = timeout
};
