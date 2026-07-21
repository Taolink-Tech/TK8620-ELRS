#pragma once

#define MS_DEBOUNCE 25       // how long the switch must change state to be considered
#define MS_LONG 500          // duration held to be considered a long press (repeats)
#define MS_MULTI_TIMEOUT 500 // duration without a press before the short count is reset

#define STATE_IDLE 0b111
#define STATE_FALL 0b100
#define STATE_RISE 0b011
#define STATE_HELD 0b000

typedef struct {
    // State
    uint8_t _pin;
    bool _idlelow;
    uint32_t _lastCheck;  // millis of last pin read
    uint32_t _lastFallingEdge; // millis of last debounced falling edge
    uint8_t _state; // pin history
    bool _isLongPress; // true if last press was a long
    uint8_t _longCount; // number of times long press has repeated
    uint8_t _pressCount; // number of short presses before timeout

    void (*OnShortPress)();
    void (*OnLongPress)();
} Button_t;
