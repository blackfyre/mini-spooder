// The start-up reset-cause line (firmware-build), from the SDK's rst_info.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ms {

// "power-on", "hardware-watchdog", "exception", "software-watchdog",
// "software-restart", "deep-sleep-wake" or "external" for SDK reasons 0-6;
// nullptr for any other value.
const char* resetCauseName(uint32_t reason);

// Whether the line carries exccause and epc1 (exception and both watchdogs).
bool resetCauseHasException(uint32_t reason);

// "reset <cause>", "reset unknown-<n>", or with " exccause <n> epc1 0x<hex>".
size_t formatResetLine(char* buf, size_t size, uint32_t reason, uint32_t exccause, uint32_t epc1);

}  // namespace ms
