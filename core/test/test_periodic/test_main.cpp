#include <unity.h>

#include "system/periodic.h"

using namespace ms;

void setUp() {}
void tearDown() {}

static void test_on_time_and_jitter() {
  TEST_ASSERT_EQUAL_UINT32(20, nextDeadline(10, 10, 10));
  TEST_ASSERT_EQUAL_UINT32(20, nextDeadline(10, 15, 10));
  TEST_ASSERT_EQUAL_UINT32(20, nextDeadline(10, 19, 10));
}

static void test_missed_intervals_not_made_up() {
  TEST_ASSERT_EQUAL_UINT32(30, nextDeadline(10, 20, 10));
  TEST_ASSERT_EQUAL_UINT32(70, nextDeadline(10, 60, 10));
}

static void test_across_the_wrap() {
  TEST_ASSERT_EQUAL_UINT32(4, nextDeadline(0xFFFFFFFAu, 0xFFFFFFFCu, 10));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFAu + 10u + 0u, nextDeadline(0xFFFFFFFAu, 0xFFFFFFFAu, 10));
  // late by more than one interval across the wrap
  TEST_ASSERT_EQUAL_UINT32(30, nextDeadline(0xFFFFFFFAu, 20, 10));
}

static void test_is_due() {
  TEST_ASSERT_TRUE(isDue(10, 10));
  TEST_ASSERT_TRUE(isDue(10, 11));
  TEST_ASSERT_FALSE(isDue(10, 9));
  TEST_ASSERT_TRUE(isDue(0xFFFFFFFAu, 4));
  TEST_ASSERT_FALSE(isDue(4, 0xFFFFFFFAu));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_on_time_and_jitter);
  RUN_TEST(test_missed_intervals_not_made_up);
  RUN_TEST(test_across_the_wrap);
  RUN_TEST(test_is_due);
  return UNITY_END();
}
