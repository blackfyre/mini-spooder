#include <unity.h>

#include "servo/pulse.h"
#include "servo/servo_bank.h"

using namespace ms;

namespace {

int hookCalls[kServoCount];
int hookTotal;

void countHook(void*, int servo) {
  ++hookCalls[servo];
  ++hookTotal;
}

const Pose kStandby = {{70, 90, 90, 110, 110, 90, 90, 70}};

}  // namespace

void setUp() {
  for (int& c : hookCalls) c = 0;
  hookTotal = 0;
}
void tearDown() {}

static void test_reset_state() {
  ServoBank bank;
  for (int i = 0; i < kServoCount; ++i) {
    TEST_ASSERT_FALSE(bank.state(i).driven);
    TEST_ASSERT_FALSE(bank.state(i).hasAngle);
    TEST_ASSERT_EQUAL_UINT16(0, bank.widthUs(i));
  }
  TEST_ASSERT_FALSE(bank.anyDriven());
}

static void test_index_8_rejected() {
  ServoBank bank;
  TEST_ASSERT_TRUE(bank.setPose(kStandby));
  TEST_ASSERT_FALSE(bank.setServo(8, 90));
  TEST_ASSERT_FALSE(bank.setServo(-1, 90));
  TEST_ASSERT_FALSE(bank.release(8));
  for (int i = 0; i < kServoCount; ++i) {
    TEST_ASSERT_TRUE(bank.state(i).driven);
    TEST_ASSERT_EQUAL_UINT8(kStandby.angle[i], bank.state(i).angle);
  }
}

static void test_out_of_range_angles_rejected() {
  ServoBank bank;
  TEST_ASSERT_TRUE(bank.setPose(kStandby));
  const uint16_t before = bank.widthUs(2);
  TEST_ASSERT_FALSE(bank.setServo(2, 181));
  TEST_ASSERT_EQUAL_UINT16(before, bank.widthUs(2));
  TEST_ASSERT_EQUAL_UINT8(90, bank.state(2).angle);

  Pose bad = kStandby;
  bad.angle[5] = -1;
  bad.angle[0] = 100;  // a valid angle in the same pose must not be applied
  TEST_ASSERT_FALSE(bank.setPose(bad));
  for (int i = 0; i < kServoCount; ++i) {
    TEST_ASSERT_EQUAL_UINT8(kStandby.angle[i], bank.state(i).angle);
  }
}

static void test_report_after_pose_with_servo_2_released() {
  ServoBank bank;
  bank.setPose(kStandby);
  TEST_ASSERT_TRUE(bank.release(2));
  for (int i = 0; i < kServoCount; ++i) {
    const ServoState s = bank.state(i);
    TEST_ASSERT_TRUE(s.hasAngle);
    TEST_ASSERT_EQUAL_UINT8(kStandby.angle[i], s.angle);
    TEST_ASSERT_EQUAL(i != 2, s.driven);
  }
  TEST_ASSERT_EQUAL_UINT16(0, bank.widthUs(2));
}

static void test_release_all() {
  ServoBank bank;
  bank.setPose(kStandby);
  bank.releaseAll();
  TEST_ASSERT_FALSE(bank.anyDriven());
  for (int i = 0; i < kServoCount; ++i) {
    TEST_ASSERT_TRUE(bank.state(i).hasAngle);
    TEST_ASSERT_EQUAL_UINT8(kStandby.angle[i], bank.state(i).angle);
  }
}

static void test_resume_after_release() {
  ServoBank bank;
  bank.setPose(kStandby);
  bank.release(5);
  TEST_ASSERT_TRUE(bank.setServo(5, 100));
  TEST_ASSERT_TRUE(bank.state(5).driven);
  TEST_ASSERT_EQUAL_UINT16(pulseWidthUs(100), bank.widthUs(5));
}

static void test_release_released_changes_nothing() {
  ServoBank bank;
  bank.setReleaseHook(countHook, nullptr);
  bank.releaseAll();  // idle: nothing driven
  TEST_ASSERT_TRUE(bank.release(3));
  TEST_ASSERT_EQUAL_INT(0, hookTotal);
  TEST_ASSERT_FALSE(bank.state(3).hasAngle);

  bank.setPose(kStandby);
  bank.release(3);
  const ServoState s = bank.state(3);
  bank.release(3);
  TEST_ASSERT_EQUAL_INT(1, hookCalls[3]);
  TEST_ASSERT_EQUAL(s.driven, bank.state(3).driven);
  TEST_ASSERT_EQUAL_UINT8(s.angle, bank.state(3).angle);
}

static void test_trim_change_on_driven_servo() {
  ServoBank bank;
  bank.setPose(kStandby);
  TEST_ASSERT_EQUAL_UINT16(1394, bank.widthUs(6));
  bank.setTrim(6, 4);
  TEST_ASSERT_EQUAL_UINT16(1439, bank.widthUs(6));
  TEST_ASSERT_EQUAL_UINT8(90, bank.state(6).angle);
}

static void test_trim_then_safe_range() {
  ServoBank bank;
  bank.setTrim(3, 5);
  bank.setServo(3, 158);
  TEST_ASSERT_EQUAL_INT(160, bank.outputAngleOf(3));
  TEST_ASSERT_EQUAL_UINT16(2176, bank.widthUs(3));
  TEST_ASSERT_EQUAL_UINT8(158, bank.state(3).angle);
  bank.setTrim(1, -1);
  bank.setServo(1, 0);
  TEST_ASSERT_EQUAL_INT(35, bank.outputAngleOf(1));
}

static void test_release_hook_once_per_newly_released() {
  ServoBank bank;
  bank.setReleaseHook(countHook, nullptr);
  bank.setPose(kStandby);
  bank.release(2);
  TEST_ASSERT_EQUAL_INT(1, hookCalls[2]);
  TEST_ASSERT_EQUAL_INT(1, hookTotal);
  bank.releaseAll();
  TEST_ASSERT_EQUAL_INT(1, hookCalls[2]);  // already released: not again
  for (int i = 0; i < kServoCount; ++i) TEST_ASSERT_EQUAL_INT(1, hookCalls[i]);
  TEST_ASSERT_EQUAL_INT(8, hookTotal);
}

static void test_release_hook_sees_released_state() {
  struct Ctx {
    ServoBank* bank;
    bool drivenInHook;
  } ctx{nullptr, true};
  ServoBank bank;
  ctx.bank = &bank;
  bank.setReleaseHook(
      [](void* c, int servo) {
        Ctx* x = static_cast<Ctx*>(c);
        x->drivenInHook = x->bank->state(servo).driven;
      },
      &ctx);
  bank.setServo(4, 90);
  bank.release(4);
  TEST_ASSERT_FALSE(ctx.drivenInHook);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reset_state);
  RUN_TEST(test_index_8_rejected);
  RUN_TEST(test_out_of_range_angles_rejected);
  RUN_TEST(test_report_after_pose_with_servo_2_released);
  RUN_TEST(test_release_all);
  RUN_TEST(test_resume_after_release);
  RUN_TEST(test_release_released_changes_nothing);
  RUN_TEST(test_trim_change_on_driven_servo);
  RUN_TEST(test_trim_then_safe_range);
  RUN_TEST(test_release_hook_once_per_newly_released);
  RUN_TEST(test_release_hook_sees_released_state);
  return UNITY_END();
}
