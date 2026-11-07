// The stock keyframe angles that kSafeRange limits with trims -1 and 0 (task 3.1).
// Empty while kSafeRange equals kDefaultSafeRange. Task 6.4 adds an entry for each
// stock keyframe angle that a measured range narrower than the default limits.
#pragma once

#include <stdint.h>

struct ExpectedLimit {
  uint8_t pm;     // stock programme number (0 = the zero pose)
  uint8_t step;   // row within the programme
  uint8_t servo;  // servo index 0-7
  int8_t trim;    // -1 or 0
};

// The sentinel keeps the array non-empty; kExpectedLimitedCount counts the real
// entries before it.
static const ExpectedLimit kExpectedLimited[] = {
    {255, 255, 255, 0},
};

static const unsigned kExpectedLimitedCount = 0;
