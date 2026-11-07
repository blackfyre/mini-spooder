// Loop-pass statistics against the 20 ms budget (firmware-build, D4).
#pragma once

#include <stdint.h>

namespace ms {

constexpr uint32_t kLoopBudgetUs = 20000;

class LoopStats {
 public:
  // One pass of `passUs`; `commitPass` when it committed calibration to flash.
  void record(uint32_t passUs, bool commitPass);

  uint32_t passes() const { return passes_; }
  uint32_t longestPassUs() const { return longestPassUs_; }
  // Passes over the budget that did not commit calibration.
  uint32_t overBudgetCount() const { return overBudget_; }
  uint32_t longestCommitPassUs() const { return longestCommitUs_; }

 private:
  uint32_t passes_ = 0;
  uint32_t longestPassUs_ = 0;
  uint32_t overBudget_ = 0;
  uint32_t longestCommitUs_ = 0;
};

}  // namespace ms
