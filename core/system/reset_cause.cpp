#include "system/reset_cause.h"

#include <stdio.h>

namespace ms {

const char* resetCauseName(uint32_t reason) {
  switch (reason) {
    case 0: return "power-on";
    case 1: return "hardware-watchdog";
    case 2: return "exception";
    case 3: return "software-watchdog";
    case 4: return "software-restart";
    case 5: return "deep-sleep-wake";
    case 6: return "external";
    default: return nullptr;
  }
}

bool resetCauseHasException(uint32_t reason) { return reason >= 1 && reason <= 3; }

size_t formatResetLine(char* buf, size_t size, uint32_t reason, uint32_t exccause, uint32_t epc1) {
  const char* name = resetCauseName(reason);
  int n;
  if (name == nullptr) {
    n = snprintf(buf, size, "reset unknown-%lu", static_cast<unsigned long>(reason));
  } else if (resetCauseHasException(reason)) {
    n = snprintf(buf, size, "reset %s exccause %lu epc1 0x%08lx", name,
                 static_cast<unsigned long>(exccause), static_cast<unsigned long>(epc1));
  } else {
    n = snprintf(buf, size, "reset %s", name);
  }
  if (n < 0) return 0;
  return static_cast<size_t>(n) < size ? static_cast<size_t>(n) : (size ? size - 1 : 0);
}

}  // namespace ms
