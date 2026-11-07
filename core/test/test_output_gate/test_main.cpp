#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "servo/output_gate.h"
#include "servo/pulse.h"
#include "servo/servo_bank.h"

using namespace ms;

namespace {

const Pose kStandby = {{70, 90, 90, 110, 110, 90, 90, 70}};
const Pose kOther = {{80, 100, 100, 100, 100, 100, 100, 80}};

// Records every mode change and the bank as the listener saw it.
struct Recorder {
  int calls = 0;
  OutputMode lastMode = OutputMode::Idle;
  char lastLine[48] = {};
  bool anyDrivenAtCall = false;
  int servo3AngleAtCall = -1;
  const ServoBank* bank = nullptr;
};

void record(void* ctx, OutputMode mode, const char* reason) {
  Recorder* r = static_cast<Recorder*>(ctx);
  ++r->calls;
  r->lastMode = mode;
  snprintf(r->lastLine, sizeof r->lastLine, "mode %s %s", modeName(mode), reason);
  r->anyDrivenAtCall = r->bank->anyDriven();
  r->servo3AngleAtCall = r->bank->state(3).hasAngle ? r->bank->state(3).angle : -1;
}

struct Fixture {
  ServoBank bank;
  OutputGate gate;
  Recorder rec;
  explicit Fixture(bool trimsValid = true, bool checkBuild = false)
      : bank(), gate(bank, trimsValid, checkBuild) {
    rec.bank = &bank;
    gate.addModeListener(record, &rec);
  }
};

struct Snapshot {
  ServoState s[kServoCount];
  uint16_t w[kServoCount];
};

Snapshot snap(const ServoBank& b) {
  Snapshot x;
  for (int i = 0; i < kServoCount; ++i) {
    x.s[i] = b.state(i);
    x.w[i] = b.widthUs(i);
  }
  return x;
}

void assertSame(const Snapshot& a, const ServoBank& b) {
  for (int i = 0; i < kServoCount; ++i) {
    TEST_ASSERT_EQUAL(a.s[i].driven, b.state(i).driven);
    TEST_ASSERT_EQUAL(a.s[i].hasAngle, b.state(i).hasAngle);
    TEST_ASSERT_EQUAL_UINT8(a.s[i].angle, b.state(i).angle);
    TEST_ASSERT_EQUAL_UINT16(a.w[i], b.widthUs(i));
  }
}

void assertPose(const ServoBank& b, const Pose& p) {
  for (int i = 0; i < kServoCount; ++i) {
    TEST_ASSERT_TRUE(b.state(i).driven);
    TEST_ASSERT_EQUAL_UINT8(p.angle[i], b.state(i).angle);
  }
}

void assertNoneDriven(const ServoBank& b) { TEST_ASSERT_FALSE(b.anyDriven()); }

void assertModeLine(const OutputGate& g, const char* expected) {
  char line[48];
  g.modeLine(line, sizeof line);
  TEST_ASSERT_EQUAL_STRING(expected, line);
}

}  // namespace

void setUp() {}
void tearDown() {}

static void test_construction() {
  Fixture ok(true);
  TEST_ASSERT_EQUAL(OutputMode::Idle, ok.gate.mode());
  assertNoneDriven(ok.bank);
  Fixture bad(false);
  TEST_ASSERT_EQUAL(OutputMode::Fault, bad.gate.mode());
  assertModeLine(bad.gate, "mode fault calibration");
  TEST_ASSERT_EQUAL_INT(0, ok.rec.calls + bad.rec.calls);
}

static void test_writes_rejected_in_idle() {
  Fixture f;
  const WriterToken t = f.gate.token();
  TEST_ASSERT_FALSE(f.gate.setServo(t, 0, 80));
  TEST_ASSERT_FALSE(f.gate.setPose(t, kStandby));
  TEST_ASSERT_FALSE(f.gate.release(t, 0));
  TEST_ASSERT_FALSE(f.gate.releaseAll(t));
  TEST_ASSERT_EQUAL(OutputMode::Idle, f.gate.mode());
  assertNoneDriven(f.bank);
  TEST_ASSERT_FALSE(f.bank.state(0).hasAngle);
}

static void test_portal_enable_in_idle() {
  Fixture f;
  const EnableResult r = f.gate.enable(Origin::Portal, kStandby);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL(OutputMode::Active, f.gate.mode());
  assertPose(f.bank, kStandby);
  TEST_ASSERT_EQUAL_UINT32(f.gate.token(), r.token);
  TEST_ASSERT_EQUAL_STRING("mode active portal", f.rec.lastLine);
  assertModeLine(f.gate, "mode active portal");
}

static void test_enable_with_invalid_pose_rejected() {
  Fixture f;
  Pose bad = kStandby;
  bad.angle[5] = -1;
  TEST_ASSERT_FALSE(f.gate.enable(Origin::Portal, bad).accepted);
  TEST_ASSERT_EQUAL(OutputMode::Idle, f.gate.mode());
  assertNoneDriven(f.bank);
}

static void test_enable_rule_after_link_loss() {
  Fixture f;
  f.gate.enable(Origin::Portal, kStandby);
  f.gate.enterSafe(SafeReason::LinkLoss);
  TEST_ASSERT_EQUAL(OutputMode::Safe, f.gate.mode());

  // portal accepted with no session
  TEST_ASSERT_TRUE(f.gate.enablePermitted(Origin::Portal));
  TEST_ASSERT_FALSE(f.gate.enablePermitted(Origin::Link));
  TEST_ASSERT_FALSE(f.gate.enable(Origin::Link, kStandby).accepted);
  TEST_ASSERT_TRUE(f.gate.enable(Origin::Portal, kStandby).accepted);
  TEST_ASSERT_EQUAL(OutputMode::Active, f.gate.mode());

  // portal rejected while a session is live
  f.gate.enterSafe(SafeReason::LinkLoss);
  f.gate.linkSessionStarted();
  TEST_ASSERT_FALSE(f.gate.enablePermitted(Origin::Portal));
  TEST_ASSERT_FALSE(f.gate.enable(Origin::Portal, kStandby).accepted);
  TEST_ASSERT_EQUAL(OutputMode::Safe, f.gate.mode());
  assertNoneDriven(f.bank);

  // link accepted: this session started after the safe state
  TEST_ASSERT_TRUE(f.gate.enablePermitted(Origin::Link));
  TEST_ASSERT_TRUE(f.gate.enable(Origin::Link, kStandby).accepted);
  TEST_ASSERT_EQUAL(OutputMode::Active, f.gate.mode());
  TEST_ASSERT_EQUAL_STRING("mode active link", f.rec.lastLine);

  // link rejected for a session that started before the safe state
  f.gate.enterSafe(SafeReason::Check);
  TEST_ASSERT_TRUE(f.gate.sessionLive());
  TEST_ASSERT_FALSE(f.gate.enablePermitted(Origin::Link));
  TEST_ASSERT_FALSE(f.gate.enable(Origin::Link, kStandby).accepted);
  TEST_ASSERT_EQUAL(OutputMode::Safe, f.gate.mode());

  // the session ends: portal accepted again, link not
  f.gate.linkSessionEnded();
  TEST_ASSERT_FALSE(f.gate.enablePermitted(Origin::Link));
  TEST_ASSERT_TRUE(f.gate.enablePermitted(Origin::Portal));
}

static void test_link_rejected_in_idle_without_session() {
  Fixture f;
  TEST_ASSERT_FALSE(f.gate.enablePermitted(Origin::Link));
  TEST_ASSERT_FALSE(f.gate.enable(Origin::Link, kStandby).accepted);
  f.gate.linkSessionStarted();
  TEST_ASSERT_TRUE(f.gate.enablePermitted(Origin::Link));
  TEST_ASSERT_TRUE(f.gate.enable(Origin::Link, kStandby).accepted);
}

static void test_listeners_called_before_return() {
  Fixture f;
  Recorder second;
  second.bank = &f.bank;
  f.gate.addModeListener(record, &second);

  f.gate.enable(Origin::Portal, kStandby);
  TEST_ASSERT_EQUAL_INT(1, f.rec.calls);
  TEST_ASSERT_EQUAL_INT(1, second.calls);
  TEST_ASSERT_TRUE(f.rec.anyDrivenAtCall);

  f.gate.enable(Origin::Portal, kOther);  // no mode change in active
  TEST_ASSERT_EQUAL_INT(1, f.rec.calls);

  f.gate.enterSafe(SafeReason::LinkLoss);
  TEST_ASSERT_EQUAL_INT(2, f.rec.calls);
  TEST_ASSERT_EQUAL(OutputMode::Safe, f.rec.lastMode);
  TEST_ASSERT_FALSE(f.rec.anyDrivenAtCall);  // released before the listener runs

  f.gate.enterSafe(SafeReason::Check);  // in safe: reason only, no mode change
  TEST_ASSERT_EQUAL_INT(2, f.rec.calls);
  TEST_ASSERT_EQUAL(SafeReason::Check, f.gate.safeReason());

  f.gate.calibrationFault();
  TEST_ASSERT_EQUAL_INT(3, f.rec.calls);
  TEST_ASSERT_EQUAL_INT(3, second.calls);
}

static void test_enter_safe_from_active() {
  Fixture f;
  const WriterToken t = f.gate.enable(Origin::Portal, kStandby).token;
  TEST_ASSERT_TRUE(f.gate.setServo(t, 1, 100));
  f.gate.enterSafe(SafeReason::LinkLoss);
  TEST_ASSERT_EQUAL(OutputMode::Safe, f.gate.mode());
  TEST_ASSERT_EQUAL(SafeReason::LinkLoss, f.gate.safeReason());
  assertNoneDriven(f.bank);
  TEST_ASSERT_NOT_EQUAL(t, f.gate.token());
  TEST_ASSERT_FALSE(f.gate.setPose(t, kStandby));
  TEST_ASSERT_FALSE(f.gate.setServo(t, 0, 80));
  assertNoneDriven(f.bank);
  // the report keeps the last commanded angles
  TEST_ASSERT_EQUAL_UINT8(100, f.bank.state(1).angle);
  TEST_ASSERT_TRUE(f.bank.state(1).hasAngle);
  TEST_ASSERT_EQUAL_STRING("mode safe link-loss", f.rec.lastLine);
}

static void test_stale_token_after_re_enable() {
  Fixture f;
  const WriterToken first = f.gate.enable(Origin::Portal, kStandby).token;
  f.gate.enterSafe(SafeReason::LinkLoss);
  const EnableResult second = f.gate.enable(Origin::Portal, kOther);
  TEST_ASSERT_TRUE(second.accepted);
  TEST_ASSERT_NOT_EQUAL(first, second.token);
  TEST_ASSERT_FALSE(f.gate.setPose(first, kStandby));
  TEST_ASSERT_FALSE(f.gate.setServo(first, 0, 30));
  TEST_ASSERT_FALSE(f.gate.release(first, 0));
  assertPose(f.bank, kOther);
  TEST_ASSERT_TRUE(f.gate.setPose(second.token, kStandby));
  assertPose(f.bank, kStandby);
}

static void test_enter_safe_in_idle() {
  Fixture f;
  f.gate.enterSafe(SafeReason::Check);
  TEST_ASSERT_EQUAL(OutputMode::Safe, f.gate.mode());
  TEST_ASSERT_EQUAL_STRING("mode safe check", f.rec.lastLine);
  assertNoneDriven(f.bank);
}

static void test_check_origin_without_flag() {
  Fixture f(true, false);
  TEST_ASSERT_FALSE(f.gate.enablePermitted(Origin::Check));
  TEST_ASSERT_FALSE(f.gate.enable(Origin::Check, kStandby).accepted);
  TEST_ASSERT_EQUAL(OutputMode::Idle, f.gate.mode());
  assertNoneDriven(f.bank);

  f.gate.enable(Origin::Portal, kStandby);
  const Snapshot before = snap(f.bank);
  TEST_ASSERT_FALSE(f.gate.enablePermitted(Origin::Check));
  TEST_ASSERT_FALSE(f.gate.enable(Origin::Check, kOther).accepted);
  assertSame(before, f.bank);
}

static void test_check_origin_with_flag() {
  Fixture f(true, true);
  TEST_ASSERT_TRUE(f.gate.enablePermitted(Origin::Check));
  TEST_ASSERT_TRUE(f.gate.enable(Origin::Check, kStandby).accepted);
  TEST_ASSERT_EQUAL_STRING("mode active check", f.rec.lastLine);
  TEST_ASSERT_TRUE(f.gate.enable(Origin::Check, kOther).accepted);
  assertPose(f.bank, kOther);

  // not in idle or safe while a session is live
  f.gate.enterSafe(SafeReason::Check);
  f.gate.linkSessionStarted();
  TEST_ASSERT_FALSE(f.gate.enablePermitted(Origin::Check));
  TEST_ASSERT_FALSE(f.gate.enable(Origin::Check, kStandby).accepted);
}

static void test_direct_servo_in_active_keeps_token() {
  Fixture f;
  const WriterToken t = f.gate.enable(Origin::Portal, kStandby).token;
  TEST_ASSERT_TRUE(f.gate.directServo(3, 80));
  TEST_ASSERT_EQUAL(OutputMode::Active, f.gate.mode());
  TEST_ASSERT_EQUAL_UINT8(80, f.bank.state(3).angle);
  TEST_ASSERT_EQUAL_UINT32(t, f.gate.token());
  TEST_ASSERT_TRUE(f.gate.setServo(t, 0, 100));
  TEST_ASSERT_EQUAL_INT(1, f.rec.calls);
}

static void assertDirectEnable(Fixture& f, WriterToken tokenBefore) {
  TEST_ASSERT_EQUAL(OutputMode::Active, f.gate.mode());
  TEST_ASSERT_EQUAL_STRING("mode active portal", f.rec.lastLine);
  for (int i = 0; i < kServoCount; ++i) {
    TEST_ASSERT_TRUE(f.bank.state(i).driven);
    const int expected = i == 3 ? 80 : kEnablePose.angle[i];
    TEST_ASSERT_EQUAL_UINT8(expected, f.bank.state(i).angle);
    TEST_ASSERT_EQUAL_UINT16(pulseWidthUs(outputAngle(i, expected, 0)), f.bank.widthUs(i));
  }
  // the listener already saw the final angle: nothing ran in between
  TEST_ASSERT_EQUAL_INT(80, f.rec.servo3AngleAtCall);
  TEST_ASSERT_EQUAL_UINT32(tokenBefore, f.gate.token());
}

static void test_direct_servo_from_idle() {
  Fixture f;
  const WriterToken before = f.gate.token();
  TEST_ASSERT_TRUE(f.gate.directServo(3, 80));
  assertDirectEnable(f, before);
}

static void test_direct_servo_from_safe() {
  Fixture f;
  f.gate.enable(Origin::Portal, kStandby);
  f.gate.enterSafe(SafeReason::LinkLoss);
  const WriterToken before = f.gate.token();
  TEST_ASSERT_TRUE(f.gate.directServo(3, 80));
  assertDirectEnable(f, before);
}

static void test_direct_servo_rejected_while_session_live() {
  // idle
  {
    Fixture f;
    f.gate.linkSessionStarted();
    const WriterToken t = f.gate.token();
    TEST_ASSERT_FALSE(f.gate.directServo(3, 80));
    TEST_ASSERT_EQUAL(OutputMode::Idle, f.gate.mode());
    assertNoneDriven(f.bank);
    TEST_ASSERT_FALSE(f.bank.state(3).hasAngle);
    TEST_ASSERT_EQUAL_UINT32(t, f.gate.token());
    TEST_ASSERT_EQUAL_INT(0, f.rec.calls);
  }
  // safe
  {
    Fixture f;
    f.gate.enterSafe(SafeReason::LinkLoss);
    f.gate.linkSessionStarted();
    const int calls = f.rec.calls;
    TEST_ASSERT_FALSE(f.gate.directServo(3, 80));
    TEST_ASSERT_EQUAL(OutputMode::Safe, f.gate.mode());
    assertNoneDriven(f.bank);
    TEST_ASSERT_EQUAL_INT(calls, f.rec.calls);
  }
  // active after the session's motion request
  {
    Fixture f;
    f.gate.linkSessionStarted();
    const WriterToken t = f.gate.enable(Origin::Link, kStandby).token;
    const Snapshot before = snap(f.bank);
    TEST_ASSERT_FALSE(f.gate.directServo(3, 80));
    TEST_ASSERT_EQUAL(OutputMode::Active, f.gate.mode());
    assertSame(before, f.bank);
    TEST_ASSERT_EQUAL_UINT32(t, f.gate.token());
  }
}

static void test_invalid_direct_servo_in_idle() {
  Fixture f;
  TEST_ASSERT_FALSE(f.gate.directServo(8, 90));
  TEST_ASSERT_FALSE(f.gate.directServo(3, 181));
  TEST_ASSERT_FALSE(f.gate.directServo(3, -1));
  TEST_ASSERT_EQUAL(OutputMode::Idle, f.gate.mode());
  assertNoneDriven(f.bank);
  TEST_ASSERT_EQUAL_INT(0, f.rec.calls);
}

static void test_direct_servo_rejected_in_fault() {
  Fixture f(false);
  TEST_ASSERT_FALSE(f.gate.directServo(3, 80));
  TEST_ASSERT_EQUAL(OutputMode::Fault, f.gate.mode());
  assertNoneDriven(f.bank);
}

static void test_portal_enable_in_active_while_session_live() {
  Fixture f;
  f.gate.linkSessionStarted();
  f.gate.enable(Origin::Link, kStandby);
  const Snapshot before = snap(f.bank);
  TEST_ASSERT_FALSE(f.gate.enablePermitted(Origin::Portal));
  TEST_ASSERT_FALSE(f.gate.enable(Origin::Portal, kOther).accepted);
  TEST_ASSERT_EQUAL(OutputMode::Active, f.gate.mode());
  assertSame(before, f.bank);
  // with no session, portal may apply its pose in active
  f.gate.linkSessionEnded();
  TEST_ASSERT_TRUE(f.gate.enablePermitted(Origin::Portal));
  TEST_ASSERT_TRUE(f.gate.enable(Origin::Portal, kOther).accepted);
  assertPose(f.bank, kOther);
}

static void test_enable_rejected_in_fault() {
  Fixture f(false, true);
  TEST_ASSERT_FALSE(f.gate.enablePermitted(Origin::Portal));
  TEST_ASSERT_FALSE(f.gate.enablePermitted(Origin::Check));
  TEST_ASSERT_FALSE(f.gate.enable(Origin::Portal, kStandby).accepted);
  TEST_ASSERT_FALSE(f.gate.enable(Origin::Check, kStandby).accepted);
  f.gate.linkSessionStarted();
  TEST_ASSERT_FALSE(f.gate.enable(Origin::Link, kStandby).accepted);
  TEST_ASSERT_EQUAL(OutputMode::Fault, f.gate.mode());
  assertNoneDriven(f.bank);
}

static void test_fault_transitions() {
  Fixture f;
  const WriterToken t = f.gate.enable(Origin::Portal, kStandby).token;
  f.gate.calibrationFault();
  TEST_ASSERT_EQUAL(OutputMode::Fault, f.gate.mode());
  TEST_ASSERT_EQUAL_STRING("mode fault calibration", f.rec.lastLine);
  assertNoneDriven(f.bank);
  TEST_ASSERT_NOT_EQUAL(t, f.gate.token());
  f.gate.enterSafe(SafeReason::LinkLoss);  // no effect in fault
  TEST_ASSERT_EQUAL(OutputMode::Fault, f.gate.mode());

  f.gate.calibrationValid();
  TEST_ASSERT_EQUAL(OutputMode::Safe, f.gate.mode());
  TEST_ASSERT_EQUAL(SafeReason::CalibrationOk, f.gate.safeReason());
  TEST_ASSERT_EQUAL_STRING("mode safe calibration-ok", f.rec.lastLine);
  assertNoneDriven(f.bank);
  // never straight to active; a following permitted enable is accepted
  TEST_ASSERT_TRUE(f.gate.enable(Origin::Portal, kStandby).accepted);
  TEST_ASSERT_EQUAL(OutputMode::Active, f.gate.mode());

  // calibrationValid outside fault changes nothing
  const int calls = f.rec.calls;
  f.gate.calibrationValid();
  TEST_ASSERT_EQUAL(OutputMode::Active, f.gate.mode());
  TEST_ASSERT_EQUAL_INT(calls, f.rec.calls);
}

static void test_fault_from_idle_and_safe() {
  Fixture a;
  a.gate.calibrationFault();
  TEST_ASSERT_EQUAL(OutputMode::Fault, a.gate.mode());
  Fixture b;
  b.gate.enterSafe(SafeReason::Check);
  b.gate.calibrationFault();
  TEST_ASSERT_EQUAL(OutputMode::Fault, b.gate.mode());
}

static void test_fault_clears_session_fact() {
  Fixture f;
  f.gate.linkSessionStarted();
  f.gate.calibrationFault();
  f.gate.calibrationValid();
  TEST_ASSERT_FALSE(f.gate.enablePermitted(Origin::Link));
}

static void test_mode_lines() {
  Fixture f(true, true);
  assertModeLine(f.gate, "mode idle reset");
  f.gate.enable(Origin::Check, kStandby);
  assertModeLine(f.gate, "mode active check");
  f.gate.enterSafe(SafeReason::Check);
  assertModeLine(f.gate, "mode safe check");
  f.gate.enable(Origin::Portal, kStandby);
  assertModeLine(f.gate, "mode active portal");
  f.gate.enterSafe(SafeReason::LinkLoss);
  assertModeLine(f.gate, "mode safe link-loss");
  f.gate.linkSessionStarted();
  f.gate.enable(Origin::Link, kStandby);
  assertModeLine(f.gate, "mode active link");
  f.gate.calibrationFault();
  assertModeLine(f.gate, "mode fault calibration");
  f.gate.calibrationValid();
  assertModeLine(f.gate, "mode safe calibration-ok");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_construction);
  RUN_TEST(test_writes_rejected_in_idle);
  RUN_TEST(test_portal_enable_in_idle);
  RUN_TEST(test_enable_with_invalid_pose_rejected);
  RUN_TEST(test_enable_rule_after_link_loss);
  RUN_TEST(test_link_rejected_in_idle_without_session);
  RUN_TEST(test_listeners_called_before_return);
  RUN_TEST(test_enter_safe_from_active);
  RUN_TEST(test_stale_token_after_re_enable);
  RUN_TEST(test_enter_safe_in_idle);
  RUN_TEST(test_check_origin_without_flag);
  RUN_TEST(test_check_origin_with_flag);
  RUN_TEST(test_direct_servo_in_active_keeps_token);
  RUN_TEST(test_direct_servo_from_idle);
  RUN_TEST(test_direct_servo_from_safe);
  RUN_TEST(test_direct_servo_rejected_while_session_live);
  RUN_TEST(test_invalid_direct_servo_in_idle);
  RUN_TEST(test_direct_servo_rejected_in_fault);
  RUN_TEST(test_portal_enable_in_active_while_session_live);
  RUN_TEST(test_enable_rejected_in_fault);
  RUN_TEST(test_fault_transitions);
  RUN_TEST(test_fault_from_idle_and_safe);
  RUN_TEST(test_fault_clears_session_fact);
  RUN_TEST(test_mode_lines);
  return UNITY_END();
}
