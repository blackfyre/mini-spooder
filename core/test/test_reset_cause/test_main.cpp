#include <string.h>
#include <unity.h>

#include "system/reset_cause.h"

using namespace ms;

void setUp() {}
void tearDown() {}

static void test_names() {
  const char* names[] = {"power-on",         "hardware-watchdog", "exception",
                         "software-watchdog", "software-restart",  "deep-sleep-wake",
                         "external"};
  for (uint32_t r = 0; r <= 6; ++r) TEST_ASSERT_EQUAL_STRING(names[r], resetCauseName(r));
  TEST_ASSERT_NULL(resetCauseName(7));
  TEST_ASSERT_NULL(resetCauseName(9));
}

static void test_lines() {
  char line[64];
  formatResetLine(line, sizeof line, 0, 0, 0);
  TEST_ASSERT_EQUAL_STRING("reset power-on", line);
  formatResetLine(line, sizeof line, 6, 4, 0x40201234);
  TEST_ASSERT_EQUAL_STRING("reset external", line);
  formatResetLine(line, sizeof line, 4, 0, 0);
  TEST_ASSERT_EQUAL_STRING("reset software-restart", line);
  formatResetLine(line, sizeof line, 5, 0, 0);
  TEST_ASSERT_EQUAL_STRING("reset deep-sleep-wake", line);
  formatResetLine(line, sizeof line, 9, 1, 2);
  TEST_ASSERT_EQUAL_STRING("reset unknown-9", line);
  formatResetLine(line, sizeof line, 3, 4, 0x40201234);
  TEST_ASSERT_EQUAL_STRING("reset software-watchdog exccause 4 epc1 0x40201234", line);
  formatResetLine(line, sizeof line, 1, 0, 0);
  TEST_ASSERT_EQUAL_STRING("reset hardware-watchdog exccause 0 epc1 0x00000000", line);
  formatResetLine(line, sizeof line, 2, 28, 0x4010abcd);
  TEST_ASSERT_EQUAL_STRING("reset exception exccause 28 epc1 0x4010abcd", line);
}

static void test_suffix_only_for_1_to_3() {
  char line[64];
  for (uint32_t r = 0; r <= 9; ++r) {
    formatResetLine(line, sizeof line, r, 7, 0x40200000);
    const bool hasSuffix = strstr(line, "exccause") != nullptr && strstr(line, "epc1") != nullptr;
    TEST_ASSERT_EQUAL(r >= 1 && r <= 3, hasSuffix);
  }
}

static void test_truncation_safe() {
  char line[8];
  formatResetLine(line, sizeof line, 3, 4, 0x40201234);
  TEST_ASSERT_EQUAL_size_t(7, strlen(line));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_names);
  RUN_TEST(test_lines);
  RUN_TEST(test_suffix_only_for_1_to_3);
  RUN_TEST(test_truncation_safe);
  return UNITY_END();
}
