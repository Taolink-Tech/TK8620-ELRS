#pragma once

#include <stdbool.h>

// Update RX output power. The receiver either uses a fixed level or follows the
// current TX power when MatchTX is enabled.
// Pass initialize=true during startup so the RX falls back to its default power
// before any synchronized power update has been received from the transmitter.
void DynamicPower_UpdateRx(bool initialize);
