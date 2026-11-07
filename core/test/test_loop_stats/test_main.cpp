#include <unity.h>

#include "system/loop_stats.h"

using namespace ms;

void setUp() {}
void tearDown() {}

static void test_over_budget() {
  LoopStats s;
  s.record(20000, false);  // exactly the budget: not over
  TEST_ASSERT_EQUAL_UINT32(0, s.overBudgetCount());
  s.record(25000, false);
  TEST_ASSERT_EQUAL_UINT32(1, s.overBudgetCount());
  TEST_ASSERT_EQUAL_UINT32(0, s.longestCommitPassUs());
}

static void test_commit_pass_excluded() {
  LoopStats s;
  s.record(25000, true);
  TEST_ASSERT_EQUAL_UINT32(0, s.overBudgetCount());
  TEST_ASSERT_EQUAL_UINT32(25000, s.longestCommitPassUs());
  s.record(18000, true);
  TEST_ASSERT_EQUAL_UINT32(25000, s.longestCommitPassUs());
  s.record(40000, true);
  TEST_ASSERT_EQUAL_UINT32(40000, s.longestCommitPassUs());
  TEST_ASSERT_EQUAL_UINT32(0, s.overBudgetCount());
}

static void test_longest_pass_kept() {
  LoopStats s;
  s.record(300, false);
  s.record(1200, false);
  s.record(800, false);
  TEST_ASSERT_EQUAL_UINT32(1200, s.longestPassUs());
  s.record(30000, true);
  TEST_ASSERT_EQUAL_UINT32(30000, s.longestPassUs());
  s.record(100, false);
  TEST_ASSERT_EQUAL_UINT32(30000, s.longestPassUs());
  TEST_ASSERT_EQUAL_UINT32(5, s.passes());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_over_budget);
  RUN_TEST(test_commit_pass_excluded);
  RUN_TEST(test_longest_pass_kept);
  return UNITY_END();
}
