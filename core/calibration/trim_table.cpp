#include "calibration/trim_table.h"

#include <stdio.h>

namespace ms {

TrimTable::TrimTable() : trim_(), writeNeeded_(false) {}

void TrimTable::decode(const uint8_t bytes[kTrimBytes]) {
  for (int i = 0; i < kServoCount; ++i) trim_[i] = static_cast<int8_t>(bytes[i]);
  writeNeeded_ = false;
}

void TrimTable::encode(uint8_t bytes[kTrimBytes]) const {
  for (int i = 0; i < kServoCount; ++i) bytes[i] = static_cast<uint8_t>(static_cast<int8_t>(trim_[i]));
}

bool TrimTable::allValid() const {
  for (int i = 0; i < kServoCount; ++i) {
    if (!valid(i)) return false;
  }
  return true;
}

TrimSetResult TrimTable::set(int servo, int value) {
  if (!isServoIndex(servo) || !isValidTrim(value)) return TrimSetResult::Rejected;
  if (trim_[servo] == value) return TrimSetResult::Unchanged;
  trim_[servo] = static_cast<int16_t>(value);
  writeNeeded_ = true;
  return TrimSetResult::Accepted;
}

bool TrimTable::reset() {
  bool changed = false;
  for (int i = 0; i < kServoCount; ++i) {
    if (trim_[i] != 0) {
      trim_[i] = 0;
      changed = true;
    }
  }
  if (changed) writeNeeded_ = true;
  return changed;
}

size_t TrimTable::formatLine(char* buf, size_t size) const {
  if (size == 0) return 0;
  size_t used = 0;
  auto append = [&](const char* fmt, int v, const char* suffix) {
    if (used >= size) return;
    const int n = snprintf(buf + used, size - used, fmt, v, suffix);
    if (n > 0) used += static_cast<size_t>(n);
  };
  const int n = snprintf(buf, size, "calibration %s", allValid() ? "ok" : "fault");
  if (n > 0) used = static_cast<size_t>(n);
  for (int i = 0; i < kServoCount; ++i) append(" %d%s", trim_[i], valid(i) ? "" : "!");
  return used < size ? used : size - 1;
}

}  // namespace ms
