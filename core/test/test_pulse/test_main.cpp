#include <unity.h>

#include "../fixtures/expected_limited.h"
#include "../fixtures/stock_keyframes.h"
#include "servo/pulse.h"

using namespace ms;

void setUp() {}
void tearDown() {}

static void test_stock_conversion() {
  TEST_ASSERT_EQUAL_UINT16(400, pulseWidthUs(1));
  TEST_ASSERT_EQUAL_UINT16(1394, pulseWidthUs(90));
  TEST_ASSERT_EQUAL_UINT16(1338, pulseWidthUs(85));
  TEST_ASSERT_EQUAL_UINT16(1439, pulseWidthUs(94));
  TEST_ASSERT_EQUAL_UINT16(2176, pulseWidthUs(160));
  TEST_ASSERT_EQUAL_UINT16(2400, pulseWidthUs(180));
  TEST_ASSERT_EQUAL_UINT16(1383, pulseWidthUs(89));
}

static void test_limit_after_trim() {
  TEST_ASSERT_EQUAL_INT(160, outputAngle(3, 158, 5));
  TEST_ASSERT_EQUAL_UINT16(2176, pulseWidthUs(outputAngle(3, 158, 5)));
  TEST_ASSERT_EQUAL_INT(35, outputAngle(1, 0, -1));
  TEST_ASSERT_EQUAL_INT(85, outputAngle(1, 90, -5));
  TEST_ASSERT_EQUAL_INT(20, outputAngle(0, 0, -124));
  TEST_ASSERT_EQUAL_INT(180, outputAngle(0, 180, 124));
}

static void test_range_shape() {
  for (int i = 0; i < kServoCount; ++i) {
    TEST_ASSERT_TRUE(1 <= kDefaultSafeRange[i].low);
    TEST_ASSERT_TRUE(kDefaultSafeRange[i].low < kDefaultSafeRange[i].high);
    TEST_ASSERT_TRUE(kDefaultSafeRange[i].high <= 180);
    TEST_ASSERT_TRUE(1 <= kSafeRange[i].low);
    TEST_ASSERT_TRUE(kSafeRange[i].low < kSafeRange[i].high);
    TEST_ASSERT_TRUE(kSafeRange[i].high <= 180);
  }
}

static void test_default_ranges_are_the_spec() {
  const uint8_t expected[8][2] = {{20, 180}, {35, 145}, {35, 145}, {35, 160},
                                  {35, 160}, {35, 145}, {35, 145}, {20, 145}};
  for (int i = 0; i < kServoCount; ++i) {
    TEST_ASSERT_EQUAL_UINT8(expected[i][0], kDefaultSafeRange[i].low);
    TEST_ASSERT_EQUAL_UINT8(expected[i][1], kDefaultSafeRange[i].high);
  }
}

static bool limitedBy(const SafeRange& r, int angle, int trim) {
  return limitedAngle(r, angle, trim) != angle + trim;
}

static void test_stock_envelope_inside_defaults() {
  TEST_ASSERT_EQUAL_UINT(124, kStockKeyframeCount);  // 123 stock rows + the zero pose
  const int trims[] = {-1, 0};
  for (unsigned k = 0; k < kStockKeyframeCount; ++k) {
    for (int i = 0; i < kServoCount; ++i) {
      for (int t : trims) {
        TEST_ASSERT_FALSE_MESSAGE(limitedBy(kDefaultSafeRange[i], kStockKeyframes[k].angle[i], t),
                                  "stock keyframe angle limited by a default range");
      }
    }
  }
}

static void test_enable_pose() {
  const int expected[8] = {135, 45, 135, 45, 45, 135, 45, 135};
  for (int i = 0; i < kServoCount; ++i) {
    TEST_ASSERT_EQUAL_INT(expected[i], kEnablePose.angle[i]);
    TEST_ASSERT_FALSE(limitedBy(kDefaultSafeRange[i], kEnablePose.angle[i], -1));
    TEST_ASSERT_FALSE(limitedBy(kDefaultSafeRange[i], kEnablePose.angle[i], 0));
  }
}

static bool isExpected(const StockKeyframe& kf, int servo, int trim) {
  for (unsigned e = 0; e < kExpectedLimitedCount; ++e) {
    const ExpectedLimit& x = kExpectedLimited[e];
    if (x.pm == kf.pm && x.step == kf.step && x.servo == servo && x.trim == trim) return true;
  }
  return false;
}

static void test_limited_by_table_in_force_are_expected() {
  unsigned limited = 0;
  const int trims[] = {-1, 0};
  for (unsigned k = 0; k < kStockKeyframeCount; ++k) {
    for (int i = 0; i < kServoCount; ++i) {
      for (int t : trims) {
        const bool lim = limitedBy(kSafeRange[i], kStockKeyframes[k].angle[i], t);
        TEST_ASSERT_EQUAL_MESSAGE(isExpected(kStockKeyframes[k], i, t), lim,
                                  "kSafeRange limits differ from expected_limited.h");
        if (lim) ++limited;
      }
    }
  }
  TEST_ASSERT_EQUAL_UINT(kExpectedLimitedCount, limited);
}

static void test_accepted_angles() {
  TEST_ASSERT_TRUE(isAcceptedAngle(0));
  TEST_ASSERT_TRUE(isAcceptedAngle(180));
  TEST_ASSERT_FALSE(isAcceptedAngle(-1));
  TEST_ASSERT_FALSE(isAcceptedAngle(181));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stock_conversion);
  RUN_TEST(test_limit_after_trim);
  RUN_TEST(test_range_shape);
  RUN_TEST(test_default_ranges_are_the_spec);
  RUN_TEST(test_stock_envelope_inside_defaults);
  RUN_TEST(test_enable_pose);
  RUN_TEST(test_limited_by_table_in_force_are_expected);
  RUN_TEST(test_accepted_angles);
  return UNITY_END();
}
