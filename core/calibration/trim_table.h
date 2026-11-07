// The per-servo calibration trims in the stock format (servo-calibration): one
// signed two's-complement byte per servo in EEPROM bytes 0-7, 0xFF reading as -1.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "servo/pulse.h"

namespace ms {

constexpr int kTrimMin = -124;
constexpr int kTrimMax = 124;
constexpr int kTrimBytes = kServoCount;

enum class TrimSetResult : uint8_t {
  Accepted,   // accepted, the table changed, a write is needed
  Unchanged,  // accepted, the value was already held, no write
  Rejected,   // index outside 0-7 or value outside -124..+124, table unchanged
};

inline bool isValidTrim(int value) { return value >= kTrimMin && value <= kTrimMax; }

class TrimTable {
 public:
  TrimTable();  // all trims 0, all valid, no write needed

  // Load bytes 0-7 as stored. Any byte is a number; out-of-range ones are invalid.
  void decode(const uint8_t bytes[kTrimBytes]);
  void encode(uint8_t bytes[kTrimBytes]) const;

  int trim(int servo) const { return trim_[servo]; }
  bool valid(int servo) const { return isValidTrim(trim_[servo]); }
  bool allValid() const;

  TrimSetResult set(int servo, int value);
  // Sets all trims to 0. Returns true when that changed at least one trim.
  bool reset();

  // Set by a change, cleared once the change is stored.
  bool writeNeeded() const { return writeNeeded_; }
  void clearWriteNeeded() { writeNeeded_ = false; }

  // "calibration ok <t0> ... <t7>" or "calibration fault <t0> ... <t7>", with "!"
  // after each invalid trim.
  size_t formatLine(char* buf, size_t size) const;

 private:
  int16_t trim_[kServoCount];
  bool writeNeeded_;
};

}  // namespace ms
