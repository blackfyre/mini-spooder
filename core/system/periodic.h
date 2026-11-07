// The default deadline rule for periodic work (firmware-build, D4).
#pragma once

#include <stdint.h>

namespace ms {

// Work due at `due` ran at `ran`. The next deadline is due + interval when
// ran - due < interval (jitter does not accumulate), otherwise ran + interval
// (missed intervals are not made up). All arithmetic is modulo 2^32.
inline uint32_t nextDeadline(uint32_t due, uint32_t ran, uint32_t interval) {
  return static_cast<uint32_t>(ran - due) < interval ? due + interval : ran + interval;
}

// Whether work due at `due` is due at `now`, across the millis() wrap.
inline bool isDue(uint32_t due, uint32_t now) {
  return static_cast<int32_t>(now - due) >= 0;
}

}  // namespace ms
