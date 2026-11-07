// The motion catalogue and the generated stock data (motion-playback tasks 2.1, 2.3).
#include <string.h>
#include <unity.h>

#include "motion/keyframe.h"
#include "motion/stock_motions.h"
#include "servo/pulse.h"

using namespace ms;

void setUp() {}
void tearDown() {}

namespace {

const uint8_t kStandbyPose[8] = {70, 90, 90, 110, 110, 90, 90, 70};
const uint8_t kZero[8] = {135, 45, 135, 45, 45, 135, 45, 135};

struct Expected {
  int id;
  const char* name;
  int keyframes;  // of the motion's own table
  uint8_t first[8];
  uint8_t last[8];
};

// The spec's keyframe counts; first and last keyframes from the stock image
// (docs/lafvin-motions.md).
const Expected kExpected[] = {
    {1, "Standby", 2, {90, 90, 90, 90, 90, 90, 90, 90}, {70, 90, 90, 110, 110, 90, 90, 70}},
    {2, "Forward", 11, {70, 90, 90, 110, 110, 90, 90, 70}, {70, 90, 90, 110, 110, 90, 90, 70}},
    {3, "Backward", 11, {70, 90, 90, 110, 110, 90, 90, 70}, {70, 90, 90, 110, 110, 90, 90, 70}},
    {4, "Left shift", 11, {70, 90, 90, 110, 110, 90, 90, 70}, {70, 90, 90, 110, 110, 90, 90, 70}},
    {5, "Right shift", 11, {70, 90, 90, 110, 110, 90, 90, 70}, {70, 90, 90, 110, 110, 90, 90, 70}},
    {6, "Turn left", 8, {70, 90, 90, 110, 110, 90, 90, 70}, {70, 90, 90, 110, 110, 90, 90, 70}},
    {7, "Turn right", 8, {70, 90, 90, 110, 110, 90, 90, 70}, {70, 90, 90, 110, 110, 90, 90, 70}},
    {8, "Lie", 1, {110, 90, 90, 70, 70, 90, 90, 110}, {110, 90, 90, 70, 70, 90, 90, 110}},
    {9, "Say Hi", 7, {70, 90, 90, 90, 90, 90, 90, 90}, {70, 90, 90, 90, 90, 90, 90, 90}},
    {10, "Fighting", 11, {120, 90, 90, 110, 60, 90, 90, 70}, {70, 90, 90, 70, 110, 90, 90, 110}},
    {11, "Push up", 11, {70, 90, 90, 110, 110, 90, 90, 70}, {70, 90, 90, 110, 110, 90, 90, 70}},
    {12, "Sleep", 2, {30, 90, 90, 150, 150, 90, 90, 30}, {30, 45, 135, 150, 150, 135, 45, 30}},
    {13, "Dance 1", 10, {90, 90, 90, 90, 90, 90, 90, 90}, {90, 90, 90, 90, 90, 90, 90, 90}},
    {14, "Dance 2", 9, {70, 45, 135, 110, 110, 135, 45, 70}, {75, 45, 135, 105, 110, 135, 45, 70}},
    {15, "Dance 3", 10, {70, 45, 45, 110, 110, 135, 135, 70}, {70, 90, 90, 110, 110, 90, 90, 70}},
};

// The duration table of design.md (ticks of 10 ms).
const struct {
  int id;
  uint32_t ticks;
} kTicks[] = {{1, 40},   {2, 165}, {3, 165}, {4, 165}, {5, 165}, {6, 120}, {7, 120}, {8, 50}, {9, 320},
              {10, 407}, {11, 718}, {12, 114}, {13, 300}, {14, 270}, {15, 300}, {99, 38}, {100, 38}};

// The motion's own table: the second for Sleep (Standby, then Sleep), else the first.
const MotionTable& ownTable(const Motion& m) { return m.id == 12 ? m.tables[1] : m.tables[0]; }

}  // namespace

static void test_catalogue_ids_and_names() {
  TEST_ASSERT_EQUAL(17, (int)kMotionCount);
  const int ids[17] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 99, 100};
  for (int i = 0; i < 17; ++i) {
    TEST_ASSERT_EQUAL(ids[i], motions()[i].id);
    TEST_ASSERT_EQUAL_PTR(&motions()[i], findMotion(ids[i]));
  }
  for (const Expected& e : kExpected) TEST_ASSERT_EQUAL_STRING(e.name, findMotion(e.id)->name);
  TEST_ASSERT_EQUAL_STRING("Centre", findMotion(99)->name);
  TEST_ASSERT_EQUAL_STRING("Zero position", findMotion(100)->name);
}

static void test_rejected_ids() {
  const int bad[] = {0, 16, 98, 101, -1, 255, 256, 1000};
  for (int id : bad) TEST_ASSERT_NULL(findMotion(id));
}

static void test_tables_counts_first_and_last() {
  for (const Expected& e : kExpected) {
    const Motion& m = *findMotion(e.id);
    TEST_ASSERT_NULL(m.direct_pose);
    const MotionTable& t = ownTable(m);
    TEST_ASSERT_EQUAL_MESSAGE(e.keyframes, t.count, e.name);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(e.first, t.frames[0].angle, 8, e.name);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(e.last, t.frames[t.count - 1].angle, 8, e.name);
  }
}

static void test_composites() {
  const Motion& hi = *findMotion(9);
  TEST_ASSERT_EQUAL(2, hi.table_count);
  TEST_ASSERT_EQUAL_PTR(stock::kSayHi, hi.tables[0].frames);
  TEST_ASSERT_EQUAL_PTR(stock::kStandby, hi.tables[1].frames);
  const Motion& sleep = *findMotion(12);
  TEST_ASSERT_EQUAL(2, sleep.table_count);
  TEST_ASSERT_EQUAL_PTR(stock::kStandby, sleep.tables[0].frames);
  TEST_ASSERT_EQUAL_PTR(stock::kSleep, sleep.tables[1].frames);
  for (const Expected& e : kExpected) {
    if (e.id != 9 && e.id != 12) TEST_ASSERT_EQUAL_MESSAGE(1, findMotion(e.id)->table_count, e.name);
  }
}

static void test_direct_poses() {
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kZero, findMotion(99)->direct_pose, 8);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kZero, findMotion(100)->direct_pose, 8);
  TEST_ASSERT_EQUAL(0, findMotion(99)->table_count);
  // The firmware's one enable pose equals motion 100's generated pose.
  for (int s = 0; s < 8; ++s) TEST_ASSERT_EQUAL(kEnablePose.angle[s], stock::kZeroPose[s]);
}

static void test_standby_keyframes() {
  const uint8_t all90[8] = {90, 90, 90, 90, 90, 90, 90, 90};
  TEST_ASSERT_EQUAL(2, (int)(sizeof stock::kStandby / sizeof stock::kStandby[0]));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(all90, stock::kStandby[0].angle, 8);
  TEST_ASSERT_EQUAL(200, stock::kStandby[0].duration_ms);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kStandbyPose, stock::kStandby[1].angle, 8);
  TEST_ASSERT_EQUAL(200, stock::kStandby[1].duration_ms);
}

static void test_motion_ticks_equal_duration_table() {
  for (const auto& t : kTicks) TEST_ASSERT_EQUAL_UINT32_MESSAGE(t.ticks, motionTicks(*findMotion(t.id)), findMotion(t.id)->name);
}

static void test_keyframe_ticks() {
  TEST_ASSERT_EQUAL_UINT32(1, keyframeTicks(0));
  TEST_ASSERT_EQUAL_UINT32(1, keyframeTicks(9));
  TEST_ASSERT_EQUAL_UINT32(1, keyframeTicks(10));
  TEST_ASSERT_EQUAL_UINT32(1, keyframeTicks(19));
  TEST_ASSERT_EQUAL_UINT32(20, keyframeTicks(200));
  TEST_ASSERT_EQUAL_UINT32(37, keyframeTicks(375));
  TEST_ASSERT_EQUAL_UINT32(6000, keyframeTicks(60000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_catalogue_ids_and_names);
  RUN_TEST(test_rejected_ids);
  RUN_TEST(test_tables_counts_first_and_last);
  RUN_TEST(test_composites);
  RUN_TEST(test_direct_poses);
  RUN_TEST(test_standby_keyframes);
  RUN_TEST(test_motion_ticks_equal_duration_table);
  RUN_TEST(test_keyframe_ticks);
  return UNITY_END();
}
