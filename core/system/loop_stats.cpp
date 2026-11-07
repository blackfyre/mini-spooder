#include "system/loop_stats.h"

namespace ms {

void LoopStats::record(uint32_t passUs, bool commitPass) {
  ++passes_;
  if (passUs > longestPassUs_) longestPassUs_ = passUs;
  if (commitPass) {
    if (passUs > longestCommitUs_) longestCommitUs_ = passUs;
  } else if (passUs > kLoopBudgetUs) {
    ++overBudget_;
  }
}

}  // namespace ms
