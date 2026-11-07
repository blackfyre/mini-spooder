#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "calibration/trim_table.h"

using namespace ms;

namespace {

// Reads a committed fixture next to this test: core/test/fixtures/<name>.
void readFixture(const char* name, uint8_t out[kTrimBytes]) {
  char path[512];
  const char* file = __FILE__;
  const char* cut = strstr(file, "test_trim_table");
  TEST_ASSERT_NOT_NULL(cut);
  snprintf(path, sizeof path, "%.*sfixtures/%s", static_cast<int>(cut - file), file, name);
  FILE* f = fopen(path, "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
  uint8_t extra;
  TEST_ASSERT_EQUAL_size_t(kTrimBytes, fread(out, 1, kTrimBytes, f));
  TEST_ASSERT_EQUAL_size_t(0, fread(&extra, 1, 1, f));  // exactly 8 bytes
  fclose(f);
}

void assertTrims(const TrimTable& t, const int expected[kServoCount]) {
  for (int i = 0; i < kServoCount; ++i) TEST_ASSERT_EQUAL_INT(expected[i], t.trim(i));
}

}  // namespace

void setUp() {}
void tearDown() {}

static void test_erased_bytes() {
  const uint8_t erased[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  TrimTable t;
  t.decode(erased);
  const int expected[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
  assertTrims(t, expected);
  TEST_ASSERT_TRUE(t.allValid());
  TEST_ASSERT_FALSE(t.writeNeeded());
  char line[96];
  t.formatLine(line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("calibration ok -1 -1 -1 -1 -1 -1 -1 -1", line);
}

static void test_example_fixture_round_trip() {
  uint8_t bytes[8];
  readFixture("trims_example.bin", bytes);
  TrimTable t;
  t.decode(bytes);
  const int expected[8] = {-3, 0, 0, 0, -7, 0, 0, 12};
  assertTrims(t, expected);
  TEST_ASSERT_TRUE(t.allValid());
  uint8_t back[8];
  t.encode(back);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes, back, 8);
  char line[96];
  t.formatLine(line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("calibration ok -3 0 0 0 -7 0 0 12", line);
}

static void test_fault_fixture() {
  uint8_t bytes[8];
  readFixture("trims_fault.bin", bytes);
  TrimTable t;
  t.decode(bytes);
  TEST_ASSERT_FALSE(t.valid(2));
  TEST_ASSERT_EQUAL_INT(-128, t.trim(2));
  for (int i = 0; i < 8; ++i) {
    if (i != 2) TEST_ASSERT_TRUE(t.valid(i));
  }
  TEST_ASSERT_FALSE(t.allValid());
  char line[96];
  t.formatLine(line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("calibration fault 0 0 -128! 0 0 0 0 0", line);
}

static void test_set() {
  TrimTable t;
  TEST_ASSERT_EQUAL(TrimSetResult::Accepted, t.set(2, 31));
  TEST_ASSERT_EQUAL_INT(31, t.trim(2));
  TEST_ASSERT_TRUE(t.writeNeeded());
  t.clearWriteNeeded();

  TEST_ASSERT_EQUAL(TrimSetResult::Accepted, t.set(0, 124));
  TEST_ASSERT_EQUAL(TrimSetResult::Accepted, t.set(1, -124));
  t.clearWriteNeeded();

  TEST_ASSERT_EQUAL(TrimSetResult::Rejected, t.set(2, 125));
  TEST_ASSERT_EQUAL(TrimSetResult::Rejected, t.set(2, -125));
  TEST_ASSERT_EQUAL_INT(31, t.trim(2));
  TEST_ASSERT_EQUAL(TrimSetResult::Rejected, t.set(9, 0));
  TEST_ASSERT_EQUAL(TrimSetResult::Rejected, t.set(-1, 0));
  TEST_ASSERT_FALSE(t.writeNeeded());
  const int expected[8] = {124, -124, 31, 0, 0, 0, 0, 0};
  assertTrims(t, expected);
}

static void test_set_to_current_value() {
  TrimTable t;
  TEST_ASSERT_EQUAL(TrimSetResult::Unchanged, t.set(3, 0));
  TEST_ASSERT_FALSE(t.writeNeeded());
}

static void test_reset() {
  TrimTable t;
  t.set(2, 12);
  t.set(4, -7);
  t.clearWriteNeeded();
  TEST_ASSERT_TRUE(t.reset());
  const int zeros[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  assertTrims(t, zeros);
  TEST_ASSERT_TRUE(t.writeNeeded());
  t.clearWriteNeeded();
  TEST_ASSERT_FALSE(t.reset());  // already zero: no write
  TEST_ASSERT_FALSE(t.writeNeeded());
}

static void test_reset_clears_fault() {
  uint8_t bytes[8];
  readFixture("trims_fault.bin", bytes);
  TrimTable t;
  t.decode(bytes);
  TEST_ASSERT_TRUE(t.reset());
  TEST_ASSERT_TRUE(t.allValid());
}

static void test_set_clears_fault() {
  uint8_t bytes[8];
  readFixture("trims_fault.bin", bytes);
  TrimTable t;
  t.decode(bytes);
  TEST_ASSERT_EQUAL(TrimSetResult::Accepted, t.set(2, 0));
  TEST_ASSERT_TRUE(t.allValid());
  TEST_ASSERT_TRUE(t.writeNeeded());
  char line[96];
  t.formatLine(line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("calibration ok 0 0 0 0 0 0 0 0", line);
}

static void test_every_byte_decodes() {
  for (int b = 0; b < 256; ++b) {
    uint8_t bytes[8] = {static_cast<uint8_t>(b), 0, 0, 0, 0, 0, 0, 0};
    TrimTable t;
    t.decode(bytes);
    const int v = static_cast<int8_t>(b);
    TEST_ASSERT_EQUAL_INT(v, t.trim(0));
    TEST_ASSERT_EQUAL(v >= -124 && v <= 124, t.valid(0));
    uint8_t back[8];
    t.encode(back);
    TEST_ASSERT_EQUAL_UINT8(b, back[0]);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_erased_bytes);
  RUN_TEST(test_example_fixture_round_trip);
  RUN_TEST(test_fault_fixture);
  RUN_TEST(test_set);
  RUN_TEST(test_set_to_current_value);
  RUN_TEST(test_reset);
  RUN_TEST(test_reset_clears_fault);
  RUN_TEST(test_set_clears_fault);
  RUN_TEST(test_every_byte_decodes);
  return UNITY_END();
}
