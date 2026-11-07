// The motion player against a recording servo-output model on a simulated clock
// (motion-playback task 2.3): one test per spec scenario.
#include <unity.h>

#include "../fixtures/motion_rig.h"
#include "motion/binding.h"
#include "motion/keyframe.h"
#include "motion/player.h"
#include "motion/stock_motions.h"

using namespace ms;
using rig::Rig;

void setUp() {}
void tearDown() {}

namespace {

const uint8_t kStandbyPose[8] = {70, 90, 90, 110, 110, 90, 90, 70};
const uint8_t kZero[8] = {135, 45, 135, 45, 45, 135, 45, 135};
const int kStandbyInt[8] = {70, 90, 90, 110, 110, 90, 90, 70};

void assertState(const Rig& r, PlayerActivity a, int motion, uint32_t kf, int pendingMotion, uint32_t pendingKf) {
  const PlayerState s = r.p.state();
  TEST_ASSERT_EQUAL((int)a, (int)s.activity);
  TEST_ASSERT_EQUAL(motion, s.motion);
  TEST_ASSERT_EQUAL_UINT32(kf, s.keyframe);
  TEST_ASSERT_EQUAL(pendingMotion, s.pendingMotion);
  TEST_ASSERT_EQUAL_UINT32(pendingKf, s.pendingKeyframe);
}

void assertIdle(const Rig& r) { assertState(r, PlayerActivity::Idle, 0, 0, 0, 0); }

// Output active at the standby pose; motion `id` requested and its first tick run at 0.
void startActive(Rig& r, int id, const uint8_t* pose = kStandbyPose) {
  r.m.activeAt(pose);
  TEST_ASSERT_TRUE(r.p.request(id, Origin::Portal));
  r.at(0);
}

// Runs until the current run has run `ticks` ticks (every 1 ms).
void untilTicks(Rig& r, uint32_t ticks) {
  while (r.p.currentRun() != nullptr && r.p.currentRun()->ticks < ticks) r.at(r.now + 1);
}

int firstWriteTo(const Rig& r, int servo, uint32_t from = 0) {
  for (const rig::Write& w : r.m.writes) {
    if (w.servo == servo && w.t >= from) return w.angle;
  }
  return -1;
}

}  // namespace

// --- Motion catalogue ---

static void test_known_id() {
  Rig r;
  r.m.activeAt(kStandbyPose);
  TEST_ASSERT_TRUE(r.p.request(2, Origin::Portal));
  r.at(0);
  assertState(r, PlayerActivity::Motion, 2, 0, 0, 0);
  r.untilIdle();
  TEST_ASSERT_EQUAL(1, (int)r.runs.size());
  TEST_ASSERT_EQUAL(2, r.runs[0].motion);
  TEST_ASSERT_TRUE(r.runs[0].complete);
  TEST_ASSERT_EQUAL_UINT32(165, r.runs[0].ticks);
}

static void test_centre() {
  Rig r;
  r.m.activeAt(kStandbyPose);
  TEST_ASSERT_TRUE(r.p.request(99, Origin::Portal));
  r.at(0);
  r.untilIdle();
  TEST_ASSERT_EQUAL(8, (int)r.m.writes.size());
  for (int s = 0; s < 8; ++s) {
    TEST_ASSERT_EQUAL(s, r.m.writes[s].servo);
    TEST_ASSERT_EQUAL(stock::kCentrePose[s], r.m.writes[s].angle);
  }
  TEST_ASSERT_EQUAL_UINT32(38, r.runs[0].ticks);
}

static void test_unknown_id_changes_nothing() {
  Rig r;
  startActive(r, 13);
  TEST_ASSERT_TRUE(r.p.request(2, Origin::Portal));
  r.every1ms(1, 500);
  const int bad[] = {16, 0, 98, 101};
  for (int id : bad) TEST_ASSERT_FALSE(r.p.request(id, Origin::Portal));
  assertState(r, PlayerActivity::Motion, 13, 0, 2, 0);
  r.untilIdle();
  TEST_ASSERT_EQUAL(2, (int)r.runs.size());
  TEST_ASSERT_EQUAL(13, r.runs[0].motion);
  TEST_ASSERT_TRUE(r.runs[0].complete);
  TEST_ASSERT_EQUAL(2, r.runs[1].motion);
}

static void test_composite_say_hi() {
  Rig r;
  startActive(r, 9);
  // Keyframe ends: after Say Hi's 7 keyframes (280 ticks) the pose is Say Hi's
  // last keyframe, after 320 the standby pose; motion 9 is current throughout.
  uint32_t ends[9];
  uint32_t acc = 0;
  for (int k = 0; k < 7; ++k) ends[k] = acc += keyframeTicks(stock::kSayHi[k].duration_ms);
  for (int k = 0; k < 2; ++k) ends[7 + k] = acc += keyframeTicks(stock::kStandby[k].duration_ms);
  TEST_ASSERT_EQUAL_UINT32(320, acc);
  int k = 0;
  while (r.p.currentRun() != nullptr && r.p.currentRun()->ticks < 320) {
    TEST_ASSERT_EQUAL(9, r.p.state().motion);
    if (r.p.currentRun()->ticks == ends[k]) {
      const Keyframe& f = k < 7 ? stock::kSayHi[k] : stock::kStandby[k - 7];
      TEST_ASSERT_TRUE_MESSAGE(r.m.poseIs(f.angle), "keyframe end pose");
      ++k;
    }
    r.at(r.now + 1);
  }
  TEST_ASSERT_EQUAL(8, k);
  TEST_ASSERT_TRUE(r.m.poseIs(kStandbyPose));
  r.untilIdle();
  TEST_ASSERT_EQUAL(1, (int)r.runs.size());
  TEST_ASSERT_EQUAL_UINT32(320, r.runs[0].ticks);
}

static void test_composite_sleep() {
  Rig r;
  startActive(r, 12, kZero);
  untilTicks(r, 40);
  TEST_ASSERT_TRUE(r.m.poseIs(kStandbyPose));
  r.untilIdle();
  TEST_ASSERT_TRUE(r.m.poseIs(stock::kSleep[1].angle));
  TEST_ASSERT_EQUAL_UINT32(114, r.runs[0].ticks);
}

// --- Current pose comes from servo output ---

static void test_direct_servo_between_motions() {
  Rig r;
  startActive(r, 1, kZero);
  r.untilIdle();
  r.m.directServo(2, 120);
  TEST_ASSERT_TRUE(r.p.request(8, Origin::Portal));
  const uint32_t t0 = r.now + 1;
  r.untilIdle();
  // Lie: servo 2 from 120 to 90 in 50 ticks: 120 - floor((30 + 25) / 50) = 119.
  TEST_ASSERT_EQUAL(119, firstWriteTo(r, 2, t0));
  TEST_ASSERT_EQUAL(90, r.m.commanded[2]);
}

static void test_servo_moved_away_from_next_keyframe() {
  Rig r;
  startActive(r, 1, kZero);
  r.untilIdle();
  r.m.directServo(0, 100);
  TEST_ASSERT_TRUE(r.p.request(2, Origin::Portal));  // Forward keyframe 1: servo 0 to 70 in 15 ticks
  const uint32_t t0 = r.now + 1;
  r.untilIdle();
  TEST_ASSERT_EQUAL(98, firstWriteTo(r, 0, t0));  // 100 - floor((30 + 7) / 15)
  // Servo 0 reaches 70 at the end of keyframe 1 (tick 15).
  int last = -1;
  for (const rig::Write& w : r.m.writes) {
    if (w.servo == 0 && w.t >= t0 && w.t <= t0 + 140) last = w.angle;
  }
  TEST_ASSERT_EQUAL(70, last);
}

// --- Keyframe interpolation ---

static void test_first_and_last_tick() {
  Rig r;
  const uint8_t start[8] = {135, 0, 0, 0, 0, 0, 0, 0};
  r.m.activeAt(start);
  const int target[8] = {90, 0, 0, 0, 0, 0, 0, 0};
  TEST_ASSERT_NOT_EQUAL(0, r.p.playKeyframe(target, 200, Origin::Portal));
  r.at(0);
  r.untilIdle();
  TEST_ASSERT_EQUAL(20, (int)r.m.writes.size());
  TEST_ASSERT_EQUAL(133, r.m.writes[0].angle);
  TEST_ASSERT_EQUAL(90, r.m.writes[19].angle);
  TEST_ASSERT_EQUAL_UINT32(190, r.m.writes[19].t);
  TEST_ASSERT_EQUAL_UINT32(20, r.runs[0].ticks);
}

static void test_duration_not_multiple_of_ten() {
  Rig r;
  const uint8_t start[8] = {90, 90, 0, 0, 0, 0, 0, 0};
  r.m.activeAt(start);
  // Servo 0 moves by 1: with floor(n/2) = 18 it first reaches 91 at tick 19
  // (floor((19 + 18) / 37) = 1); servo 1 moves by 37, one degree per tick.
  const int target[8] = {91, 127, 0, 0, 0, 0, 0, 0};
  TEST_ASSERT_NOT_EQUAL(0, r.p.playKeyframe(target, 375, Origin::Portal));
  r.at(0);
  r.untilIdle();
  TEST_ASSERT_EQUAL_UINT32(37, r.runs[0].ticks);
  int firstTickServo0 = -1;
  for (const rig::Write& w : r.m.writes) {
    if (w.servo == 0 && w.angle == 91) {
      firstTickServo0 = (int)(w.t / 10) + 1;
      break;
    }
  }
  TEST_ASSERT_EQUAL(19, firstTickServo0);
  TEST_ASSERT_EQUAL(91, r.m.commanded[0]);
  TEST_ASSERT_EQUAL(127, r.m.commanded[1]);
  TEST_ASSERT_EQUAL_UINT32(360, r.m.writes.back().t);  // tick 37
}

static void test_duration_under_ten() {
  const int durations[] = {0, 9};
  for (int d : durations) {
    Rig r;
    r.m.activeAt(kZero);
    TEST_ASSERT_NOT_EQUAL(0, r.p.playKeyframe(kStandbyInt, d, Origin::Portal));
    r.at(0);
  r.untilIdle();
    TEST_ASSERT_EQUAL_UINT32(1, r.runs[0].ticks);
    TEST_ASSERT_TRUE(r.m.poseIs(kStandbyPose));
    for (const rig::Write& w : r.m.writes) TEST_ASSERT_EQUAL_UINT32(0, w.t);
  }
}

static void test_standby_from_zero_ends_exactly() {
  Rig r;
  startActive(r, 1, kZero);
  r.untilIdle();
  TEST_ASSERT_TRUE(r.m.poseIs(kStandbyPose));
}

static void test_unchanged_servo_not_written() {
  Rig r;
  r.m.activeAt(kStandbyPose);
  int target[8] = {70, 90, 90, 110, 110, 90, 90, 100};  // only servo 7 changes
  TEST_ASSERT_NOT_EQUAL(0, r.p.playKeyframe(target, 200, Origin::Portal));
  r.at(0);
  r.untilIdle();
  TEST_ASSERT_EQUAL(20, (int)r.m.writes.size());
  for (const rig::Write& w : r.m.writes) TEST_ASSERT_EQUAL(7, w.servo);
}

// --- Playback timing ---

static void test_on_time_ticks() {
  Rig r;
  startActive(r, 2, kZero);
  r.untilIdle();
  TEST_ASSERT_EQUAL(165, (int)r.tickTimes.size());
  for (int i = 0; i < 165; ++i) TEST_ASSERT_EQUAL_UINT32(10u * i, r.tickTimes[i]);
  TEST_ASSERT_EQUAL_UINT32(1650, r.runs[0].endMs);
}

static void test_consecutive_motions() {
  Rig r;
  startActive(r, 2, kZero);
  TEST_ASSERT_TRUE(r.p.request(2, Origin::Portal));
  r.untilIdle();
  TEST_ASSERT_EQUAL(2, (int)r.runs.size());
  TEST_ASSERT_EQUAL_UINT32(1650, r.runs[1].startMs);
  TEST_ASSERT_EQUAL_UINT32(1650, r.runs[0].endMs);
  TEST_ASSERT_EQUAL_UINT32(1650 + 1650, r.runs[1].endMs);
}

static void test_slightly_late_tick() {
  Rig r;
  r.m.activeAt(kZero);
  TEST_ASSERT_NOT_EQUAL(0, r.p.playKeyframe(kStandbyInt, 1000, Origin::Portal));
  r.at(0);
  r.at(19);  // the tick due at 10 runs at 19
  r.every1ms(20, 45);
  const uint32_t want[] = {0, 19, 20, 30, 40};
  TEST_ASSERT_EQUAL(5, (int)r.tickTimes.size());
  for (int i = 0; i < 5; ++i) TEST_ASSERT_EQUAL_UINT32(want[i], r.tickTimes[i]);
}

static void test_stall_longer_than_a_tick() {
  Rig r;
  r.m.activeAt(kZero);
  TEST_ASSERT_NOT_EQUAL(0, r.p.playKeyframe(kStandbyInt, 200, Origin::Portal));
  r.at(0);
  r.at(60);  // due at 10
  TEST_ASSERT_EQUAL(2, (int)r.tickTimes.size());
  TEST_ASSERT_EQUAL_UINT32(60, r.tickTimes[1]);
  r.every1ms(61, 69);
  TEST_ASSERT_EQUAL(2, (int)r.tickTimes.size());
  r.untilIdle();
  TEST_ASSERT_EQUAL_UINT32(70, r.tickTimes[2]);
  TEST_ASSERT_EQUAL_UINT32(20, r.runs[0].ticks);
  TEST_ASSERT_EQUAL_UINT32(250, r.runs[0].endMs);  // lengthened by the 50 ms stall
  TEST_ASSERT_TRUE(r.m.poseIs(kStandbyPose));
}

static void test_late_chance_at_end_of_motion() {
  Rig r;
  startActive(r, 2, kZero);
  TEST_ASSERT_TRUE(r.p.request(6, Origin::Portal));
  r.every1ms(1, 1640);
  TEST_ASSERT_EQUAL_UINT32(165, r.p.currentRun()->ticks);
  r.at(1700);
  TEST_ASSERT_EQUAL_UINT32(1700, r.runs[0].endMs);
  TEST_ASSERT_EQUAL(6, r.p.state().motion);
  TEST_ASSERT_EQUAL_UINT32(1700, r.p.currentRun()->startMs);
  TEST_ASSERT_EQUAL_UINT32(1, r.p.currentRun()->ticks);
  TEST_ASSERT_EQUAL_UINT32(1700, r.tickTimes.back());
}

// --- Direct-pose routine ---

static void test_zero_position_routine() {
  Rig r;
  r.m.activeAt(kStandbyPose);
  TEST_ASSERT_TRUE(r.p.request(100, Origin::Portal));
  r.at(0);
  r.every1ms(1, 379);
  TEST_ASSERT_EQUAL(100, r.p.state().motion);
  TEST_ASSERT_EQUAL(8, (int)r.m.writes.size());
  for (int s = 0; s < 8; ++s) {
    TEST_ASSERT_EQUAL(s, r.m.writes[s].servo);
    TEST_ASSERT_EQUAL(kZero[s], r.m.writes[s].angle);
    TEST_ASSERT_EQUAL_UINT32(10u * s, r.m.writes[s].t);
  }
  TEST_ASSERT_EQUAL(38, (int)r.tickTimes.size());
  r.at(380);
  assertIdle(r);
  TEST_ASSERT_EQUAL_UINT32(380, r.runs[0].endMs);
}

// --- Still until the first command ---

static void test_no_command_after_reset() {
  Rig r;
  assertIdle(r);
  r.every1ms(0, 10000);
  TEST_ASSERT_EQUAL(0, (int)r.m.writes.size());
  TEST_ASSERT_EQUAL(0, (int)r.m.enables.size());
  TEST_ASSERT_EQUAL(0, (int)r.tickTimes.size());
  assertIdle(r);
}

// --- Output enable and the safe state ---

static void test_first_command_after_reset() {
  Rig r;
  r.every1ms(0, 100);
  TEST_ASSERT_TRUE(r.p.request(1, Origin::Portal));
  r.at(101);
  TEST_ASSERT_EQUAL(1, (int)r.m.enables.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kZero, r.m.enables[0].angle, 8);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(findMotion(100)->direct_pose, r.m.enables[0].angle, 8);
  TEST_ASSERT_EQUAL(0, (int)r.m.writes.size());  // nothing else in the enable tick
  assertState(r, PlayerActivity::Motion, 1, 0, 0, 0);
  r.untilIdle();
  TEST_ASSERT_EQUAL_UINT32(41, r.runs[0].ticks);
  TEST_ASSERT_EQUAL(41, (int)r.tickTimes.size());
  TEST_ASSERT_EQUAL_UINT32(111, r.m.writes.front().t);
  TEST_ASSERT_EQUAL(0, r.m.writes.front().servo);
  TEST_ASSERT_EQUAL(133, r.m.writes.front().angle);  // servo 0: 135 -> 90, first step from the zero pose
  TEST_ASSERT_TRUE(r.m.poseIs(kStandbyPose));
  TEST_ASSERT_EQUAL(1, (int)r.m.enables.size());
}

static void test_origin_of_enable_request() {
  const Origin origins[] = {Origin::Portal, Origin::Link, Origin::Check};
  for (Origin o : origins) {
    Rig r;
    TEST_ASSERT_TRUE(r.p.request(1, o));
    r.at(0);
    TEST_ASSERT_EQUAL(1, (int)r.m.enables.size());
    TEST_ASSERT_EQUAL((int)o, (int)r.m.enables[0].origin);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kZero, r.m.enables[0].angle, 8);
  }
  // Through a binding: the binding's origin.
  Rig r;
  MotionBinding link(r.p, Origin::Link);
  TEST_ASSERT_TRUE(link.request(1));
  r.at(0);
  TEST_ASSERT_EQUAL((int)Origin::Link, (int)r.m.enables[0].origin);
}

static void test_safe_state_during_motion() {
  Rig r;
  startActive(r, 2, kZero);
  TEST_ASSERT_TRUE(r.p.request(6, Origin::Portal));
  untilTicks(r, 40);  // tick 40 at 390
  const size_t writes = r.m.writes.size();
  r.m.enterSafe();
  r.at(r.now + 3);  // between two deadlines
  assertIdle(r);
  r.every1ms(r.now + 1, 5000);
  TEST_ASSERT_EQUAL(writes, r.m.writes.size());
  TEST_ASSERT_EQUAL(0, r.m.refusedWrites);
  TEST_ASSERT_EQUAL(1, (int)r.runs.size());
  TEST_ASSERT_FALSE(r.runs[0].complete);
  TEST_ASSERT_EQUAL_UINT32(40, r.runs[0].ticks);
  assertIdle(r);
}

static void test_write_refused() {
  Rig r;
  startActive(r, 2, kZero);
  TEST_ASSERT_TRUE(r.p.request(6, Origin::Portal));
  untilTicks(r, 39);
  const size_t writes = r.m.writes.size();
  r.m.refuseWrites = true;
  untilTicks(r, 40);
  assertIdle(r);
  TEST_ASSERT_EQUAL(1, r.m.refusedWrites);
  r.m.refuseWrites = false;
  r.every1ms(r.now + 1, 5000);
  TEST_ASSERT_EQUAL(writes, r.m.writes.size());
  TEST_ASSERT_EQUAL(1, (int)r.runs.size());
  assertIdle(r);
}

static void test_request_after_safe_state() {
  Rig r;
  startActive(r, 1, kZero);
  r.untilIdle();
  r.m.enterSafe();
  TEST_ASSERT_TRUE(r.p.request(2, Origin::Portal));
  const uint32_t t0 = r.now + 1;
  r.at(t0);
  TEST_ASSERT_EQUAL(2, (int)r.m.enables.size());  // the first motion's, then this one
  TEST_ASSERT_EQUAL_UINT32(t0, r.m.enables.back().t);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kZero, r.m.enables.back().angle, 8);
  TEST_ASSERT_EQUAL(0, (int)r.writesBetween(t0, t0));
  r.untilIdle();
  TEST_ASSERT_EQUAL_UINT32(166, r.runs.back().ticks);
  TEST_ASSERT_TRUE(r.runs.back().complete);
  TEST_ASSERT_EQUAL(0, r.m.refusedWrites);
  // Forward keyframe 1 from the zero pose: servo 0 135 -> 70 in 15 ticks.
  TEST_ASSERT_EQUAL(135 - (65 + 7) / 15, firstWriteTo(r, 0, t0));
}

static void test_enable_refused() {
  Rig r;
  r.m.refuseEnable = true;
  TEST_ASSERT_TRUE(r.p.request(2, Origin::Portal));
  r.at(0);
  assertIdle(r);
  r.every1ms(1, 3000);
  TEST_ASSERT_EQUAL(0, (int)r.m.writes.size());
  TEST_ASSERT_EQUAL(1, (int)r.m.enables.size());  // never retried
  TEST_ASSERT_FALSE(r.runs[0].complete);
  assertIdle(r);
  // Active output that refuses (for example origin not permitted): same.
  Rig a;
  a.m.activeAt(kStandbyPose);
  a.m.refuseEnable = true;
  TEST_ASSERT_TRUE(a.p.request(2, Origin::Link));
  a.at(0);
  assertIdle(a);
  TEST_ASSERT_EQUAL(0, (int)a.m.writes.size());
}

static void test_output_enabled_by_direct_servo_command() {
  Rig r;
  startActive(r, 1, kZero);
  r.untilIdle();
  r.m.enterSafe();  // the player's token is now stale
  r.every1ms(r.now + 1, r.now + 100);
  r.m.directServo(2, 120);
  uint8_t expected[8];
  for (int s = 0; s < 8; ++s) expected[s] = kZero[s];
  expected[2] = 120;
  TEST_ASSERT_TRUE(r.m.poseIs(expected));
  const size_t enables = r.m.enables.size();
  TEST_ASSERT_TRUE(r.p.request(8, Origin::Portal));
  const uint32_t t0 = r.now + 1;
  r.at(t0);
  TEST_ASSERT_EQUAL(enables + 1, r.m.enables.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, r.m.enables.back().angle, 8);
  // Lie's first tick in the same tick, from 120 on servo 2.
  TEST_ASSERT_TRUE(r.writesBetween(t0, t0) > 0);
  TEST_ASSERT_EQUAL(119, firstWriteTo(r, 2, t0));
  r.untilIdle();
  TEST_ASSERT_EQUAL_UINT32(50, r.runs.back().ticks);
  TEST_ASSERT_EQUAL(0, r.m.refusedWrites);
  TEST_ASSERT_TRUE(r.m.poseIs(stock::kLie[0].angle));
}

static void test_direct_servo_does_not_start_player() {
  Rig r;
  r.every1ms(0, 100);
  r.m.directServo(3, 100);
  r.every1ms(101, 3000);
  TEST_ASSERT_EQUAL(0, (int)r.m.writes.size());
  TEST_ASSERT_EQUAL(0, (int)r.m.enables.size());
  assertIdle(r);
}

// --- Single keyframe ---

static void test_keyframe_while_idle() {
  Rig r;
  r.m.activeAt(kZero);
  const uint32_t k = r.p.playKeyframe(kStandbyInt, 500, Origin::Portal);
  TEST_ASSERT_NOT_EQUAL(0, k);
  assertState(r, PlayerActivity::Idle, 0, 0, 0, k);
  r.at(7);
  assertState(r, PlayerActivity::Keyframe, 0, k, 0, 0);
  TEST_ASSERT_EQUAL_UINT32(7, r.tickTimes[0]);
  r.untilIdle();
  TEST_ASSERT_EQUAL_UINT32(50, r.runs[0].ticks);
  TEST_ASSERT_EQUAL((int)PlayerActivity::Keyframe, (int)r.runs[0].kind);
  TEST_ASSERT_TRUE(r.m.poseIs(kStandbyPose));
}

static void test_keyframe_during_motion() {
  Rig r;
  startActive(r, 13);
  TEST_ASSERT_TRUE(r.p.request(3, Origin::Portal));
  r.every1ms(1, 100);
  const uint32_t k = r.p.playKeyframe(kStandbyInt, 300, Origin::Portal);
  TEST_ASSERT_NOT_EQUAL(0, k);
  assertState(r, PlayerActivity::Motion, 13, 0, 0, k);
  r.untilIdle();
  TEST_ASSERT_EQUAL(2, (int)r.runs.size());
  TEST_ASSERT_EQUAL(13, r.runs[0].motion);
  TEST_ASSERT_TRUE(r.runs[0].complete);
  TEST_ASSERT_EQUAL_UINT32(k, r.runs[1].keyframe);
  TEST_ASSERT_EQUAL_UINT32(r.runs[0].endMs, r.runs[1].startMs);
  TEST_ASSERT_EQUAL(1, (int)r.m.enables.size());  // only the idle start's
}

static void test_keyframe_ended() {
  Rig r;
  r.m.activeAt(kZero);
  const uint32_t k = r.p.playKeyframe(kStandbyInt, 500, Origin::Portal);
  TEST_ASSERT_FALSE(r.p.keyframeEnded(k));
  r.at(0);
  for (uint32_t t = 1; t < 500; ++t) {
    r.at(t);
    TEST_ASSERT_FALSE(r.p.keyframeEnded(k));
  }
  r.every1ms(500, 600);
  TEST_ASSERT_TRUE(r.p.keyframeEnded(k));
  TEST_ASSERT_TRUE(r.p.keyframeEnded(0));
  TEST_ASSERT_TRUE(r.p.keyframeEnded(k + 1));  // never issued
}

static void test_pending_keyframe_not_ended() {
  Rig r;
  startActive(r, 2);
  const uint32_t k = r.p.playKeyframe(kStandbyInt, 500, Origin::Portal);
  r.every1ms(1, 100);
  TEST_ASSERT_FALSE(r.p.keyframeEnded(k));
  TEST_ASSERT_TRUE(r.p.request(4, Origin::Portal));
  TEST_ASSERT_TRUE(r.p.keyframeEnded(k));
  r.untilIdle();
  TEST_ASSERT_EQUAL(4, r.runs[1].motion);
}

static void test_keyframe_stopped() {
  Rig r;
  r.m.activeAt(kZero);
  const uint32_t k = r.p.playKeyframe(kStandbyInt, 500, Origin::Portal);
  r.every1ms(0, 100);
  TEST_ASSERT_FALSE(r.p.keyframeEnded(k));
  r.p.stop();
  TEST_ASSERT_TRUE(r.p.keyframeEnded(k));
  assertIdle(r);
}

static void test_zero_duration_keyframe() {
  Rig r;
  r.m.activeAt(kZero);
  const uint32_t k = r.p.playKeyframe(kStandbyInt, 0, Origin::Portal);
  r.at(0);
  TEST_ASSERT_TRUE(r.m.poseIs(kStandbyPose));
  TEST_ASSERT_EQUAL(1, (int)r.tickTimes.size());
  r.every1ms(1, 9);
  TEST_ASSERT_FALSE(r.p.keyframeEnded(k));
  r.at(10);
  TEST_ASSERT_TRUE(r.p.keyframeEnded(k));
  assertIdle(r);
}

static void test_invalid_keyframe() {
  Rig r;
  startActive(r, 2);
  TEST_ASSERT_TRUE(r.p.request(6, Origin::Portal));
  r.every1ms(1, 100);
  int bad[8] = {70, 90, 90, 110, 110, 90, 90, 181};
  TEST_ASSERT_EQUAL_UINT32(0, r.p.playKeyframe(bad, 500, Origin::Portal));
  bad[7] = -1;
  TEST_ASSERT_EQUAL_UINT32(0, r.p.playKeyframe(bad, 500, Origin::Portal));
  TEST_ASSERT_EQUAL_UINT32(0, r.p.playKeyframe(kStandbyInt, 60001, Origin::Portal));
  TEST_ASSERT_EQUAL_UINT32(0, r.p.playKeyframe(kStandbyInt, -1, Origin::Portal));
  TEST_ASSERT_EQUAL_UINT32(0, r.p.playKeyframe(nullptr, 500, Origin::Portal));
  assertState(r, PlayerActivity::Motion, 2, 0, 6, 0);
  // The limits themselves are accepted.
  const int edge[8] = {0, 180, 0, 180, 0, 180, 0, 180};
  const uint32_t k1 = r.p.playKeyframe(edge, 60000, Origin::Portal);
  TEST_ASSERT_NOT_EQUAL(0, k1);
  const uint32_t k2 = r.p.playKeyframe(edge, 0, Origin::Portal);
  TEST_ASSERT_NOT_EQUAL(0, k2);
  TEST_ASSERT_NOT_EQUAL(k1, k2);
}

// --- Requests while a motion plays ---

static void test_latest_request_wins() {
  Rig r;
  startActive(r, 2);
  TEST_ASSERT_TRUE(r.p.request(4, Origin::Portal));
  TEST_ASSERT_TRUE(r.p.request(5, Origin::Portal));
  r.untilIdle();
  TEST_ASSERT_EQUAL(2, (int)r.runs.size());
  TEST_ASSERT_EQUAL(2, r.runs[0].motion);
  TEST_ASSERT_TRUE(r.runs[0].complete);
  TEST_ASSERT_EQUAL(5, r.runs[1].motion);
}

static void test_same_motion_repeated() {
  Rig r;
  startActive(r, 2, kZero);
  r.every1ms(1, 500);
  TEST_ASSERT_TRUE(r.p.request(2, Origin::Portal));
  r.every1ms(501, 1649);
  const size_t before = r.m.writes.size();
  r.at(1650);
  TEST_ASSERT_EQUAL_UINT32(1650, r.p.currentRun()->startMs);
  TEST_ASSERT_EQUAL(2, r.p.state().motion);
  // Forward's first keyframe is its last keyframe's pose: nothing moves.
  TEST_ASSERT_EQUAL(before, r.m.writes.size());
  TEST_ASSERT_TRUE(r.m.poseIs(stock::kForward[10].angle));
  r.untilIdle();
  TEST_ASSERT_EQUAL(2, (int)r.runs.size());
  TEST_ASSERT_EQUAL_UINT32(165, r.runs[1].ticks);
}

static void test_played_once() {
  Rig r;
  startActive(r, 2, kZero);
  r.untilIdle();
  const size_t writes = r.m.writes.size();
  r.every1ms(r.now + 1, r.now + 5000);
  TEST_ASSERT_EQUAL(1, (int)r.runs.size());
  TEST_ASSERT_EQUAL_UINT32(165, r.runs[0].ticks);
  TEST_ASSERT_EQUAL(165, (int)r.tickTimes.size());
  TEST_ASSERT_EQUAL(writes, r.m.writes.size());
  assertIdle(r);
}

// --- Stop ---

static void test_stop_mid_motion() {
  Rig r;
  startActive(r, 2, kZero);
  TEST_ASSERT_TRUE(r.p.request(6, Origin::Portal));
  untilTicks(r, 40);
  const size_t writes = r.m.writes.size();
  r.p.stop();
  assertIdle(r);
  r.every1ms(r.now + 1, r.now + 3000);
  TEST_ASSERT_EQUAL(writes, r.m.writes.size());
  TEST_ASSERT_EQUAL(1, (int)r.runs.size());
  TEST_ASSERT_FALSE(r.runs[0].complete);
  TEST_ASSERT_EQUAL_UINT32(40, r.runs[0].ticks);
}

static void test_stop_before_request_starts() {
  Rig r;
  TEST_ASSERT_TRUE(r.p.request(2, Origin::Portal));
  r.p.stop();
  r.every1ms(0, 3000);
  TEST_ASSERT_EQUAL(0, (int)r.m.writes.size());
  TEST_ASSERT_EQUAL(0, (int)r.m.enables.size());
  TEST_ASSERT_EQUAL(0, (int)r.runs.size());
  assertIdle(r);
  // Stop with nothing playing or pending has no effect.
  r.p.stop();
  assertIdle(r);
}

static void test_start_pose_after_stop() {
  Rig r;
  startActive(r, 2, kZero);
  r.every1ms(1, 52);  // mid keyframe 1 (tick 6 of 15)
  r.p.stop();
  int last[8];
  for (int s = 0; s < 8; ++s) last[s] = r.m.commanded[s];
  TEST_ASSERT_NOT_EQUAL(135, last[0]);
  TEST_ASSERT_NOT_EQUAL(70, last[0]);
  TEST_ASSERT_TRUE(r.p.request(1, Origin::Portal));
  const uint32_t t0 = r.now + 1;
  r.at(t0);
  // Output active: the enable request carries the commanded angles, not the zero pose.
  TEST_ASSERT_EQUAL(2, (int)r.m.enables.size());
  for (int s = 0; s < 8; ++s) TEST_ASSERT_EQUAL(last[s], r.m.enables.back().angle[s]);
  // Standby keyframe 1: servo 0 to 90 in 20 ticks from the last written angle.
  const int a = last[0];
  const int expect = a > 90 ? a - (int)(((uint32_t)(a - 90) + 10) / 20) : a + (int)(((uint32_t)(90 - a) + 10) / 20);
  TEST_ASSERT_EQUAL(expect, firstWriteTo(r, 0, t0));
}

// --- Non-blocking playback ---

static void test_stop_during_long_motion() {
  Rig r;
  startActive(r, 11);
  r.every1ms(1, 2000);
  TEST_ASSERT_EQUAL(11, r.p.state().motion);
  const size_t writes = r.m.writes.size();
  r.p.stop();
  assertIdle(r);
  r.at(2001);
  TEST_ASSERT_EQUAL(writes, r.m.writes.size());
}

// --- Angles pass through ---

static void test_no_clamping_of_170() {
  Rig r;
  startActive(r, 9);
  r.untilIdle();
  bool saw170 = false;
  for (const rig::Write& w : r.m.writes) {
    if (w.servo == 0 && w.angle == 170) saw170 = true;
  }
  TEST_ASSERT_TRUE(saw170);
}

// --- Player state report ---

static void test_state_during_motion() {
  Rig r;
  startActive(r, 2);
  TEST_ASSERT_TRUE(r.p.request(6, Origin::Portal));
  r.every1ms(1, 300);
  assertState(r, PlayerActivity::Motion, 2, 0, 6, 0);
}

static void test_state_when_idle_and_before_first_command() {
  Rig r;
  assertIdle(r);
  TEST_ASSERT_TRUE(r.p.request(2, Origin::Portal));
  assertState(r, PlayerActivity::Idle, 0, 0, 2, 0);  // pending until its first tick
  r.at(0);
  assertState(r, PlayerActivity::Motion, 2, 0, 0, 0);  // current from the enable tick
  r.untilIdle();
  assertIdle(r);
}

static void test_keyframe_identifiers_distinct() {
  Rig r;
  uint32_t prev = 0;
  for (int i = 0; i < 1000; ++i) {
    const uint32_t k = r.p.playKeyframe(kStandbyInt, 100, Origin::Portal);
    TEST_ASSERT_NOT_EQUAL(0, k);
    TEST_ASSERT_TRUE(k > prev);
    prev = k;
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_known_id);
  RUN_TEST(test_centre);
  RUN_TEST(test_unknown_id_changes_nothing);
  RUN_TEST(test_composite_say_hi);
  RUN_TEST(test_composite_sleep);
  RUN_TEST(test_direct_servo_between_motions);
  RUN_TEST(test_servo_moved_away_from_next_keyframe);
  RUN_TEST(test_first_and_last_tick);
  RUN_TEST(test_duration_not_multiple_of_ten);
  RUN_TEST(test_duration_under_ten);
  RUN_TEST(test_standby_from_zero_ends_exactly);
  RUN_TEST(test_unchanged_servo_not_written);
  RUN_TEST(test_on_time_ticks);
  RUN_TEST(test_consecutive_motions);
  RUN_TEST(test_slightly_late_tick);
  RUN_TEST(test_stall_longer_than_a_tick);
  RUN_TEST(test_late_chance_at_end_of_motion);
  RUN_TEST(test_zero_position_routine);
  RUN_TEST(test_no_command_after_reset);
  RUN_TEST(test_first_command_after_reset);
  RUN_TEST(test_origin_of_enable_request);
  RUN_TEST(test_safe_state_during_motion);
  RUN_TEST(test_write_refused);
  RUN_TEST(test_request_after_safe_state);
  RUN_TEST(test_enable_refused);
  RUN_TEST(test_output_enabled_by_direct_servo_command);
  RUN_TEST(test_direct_servo_does_not_start_player);
  RUN_TEST(test_keyframe_while_idle);
  RUN_TEST(test_keyframe_during_motion);
  RUN_TEST(test_keyframe_ended);
  RUN_TEST(test_pending_keyframe_not_ended);
  RUN_TEST(test_keyframe_stopped);
  RUN_TEST(test_zero_duration_keyframe);
  RUN_TEST(test_invalid_keyframe);
  RUN_TEST(test_latest_request_wins);
  RUN_TEST(test_same_motion_repeated);
  RUN_TEST(test_played_once);
  RUN_TEST(test_stop_mid_motion);
  RUN_TEST(test_stop_before_request_starts);
  RUN_TEST(test_start_pose_after_stop);
  RUN_TEST(test_stop_during_long_motion);
  RUN_TEST(test_no_clamping_of_170);
  RUN_TEST(test_state_during_motion);
  RUN_TEST(test_state_when_idle_and_before_first_command);
  RUN_TEST(test_keyframe_identifiers_distinct);
  return UNITY_END();
}
