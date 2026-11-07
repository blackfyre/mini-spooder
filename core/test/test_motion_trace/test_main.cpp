// Trace test (motion-playback task 2.4): a reference model of the stock loop,
// written from the disassembly recorded in docs/stock-motions.md and
// docs/lafvin-motions.md (keyframe loop flash 0x1420-0x14FE, dispatch
// 0x5084-0x51E3, direct poses 0x1380/0x13D0), independently of the player.
// For motions 1-15, 99 and 100, from the zero pose and from the standby pose,
// the player must run stock's tick count per keyframe, write each keyframe's
// angles exactly at its last tick for every servo that changes, leave servo
// output's commanded angles on the keyframe angles at each keyframe end, and
// total design.md's duration table.
#include <stdio.h>
#include <unity.h>

#include <vector>

#include "../fixtures/motion_rig.h"
#include "motion/stock_motions.h"

using namespace ms;

void setUp() {}
void tearDown() {}

namespace {

const uint8_t kStandbyPose[8] = {70, 90, 90, 110, 110, 90, 90, 70};
const uint8_t kZero[8] = {135, 45, 135, 45, 45, 135, 45, 135};

// One stock keyframe step of the reference: the ticks it takes and the angles
// that the cached pose holds afterwards. `changed` marks the servos that the
// stock loop writes in it (cached angle != keyframe angle).
struct RefStep {
  uint32_t ticks;
  uint8_t angle[8];
  bool changed[8];
  int lastStockWrite[8];  // stock's last interpolated write (index n - 1), -1 if none
};

// The stock loop for one table row: floor(T / 10) steps from index 0, each
// writing A + (B - A) * i / n to the servos whose cached angle differs, then
// the cache becomes the row.
void stockRow(std::vector<RefStep>& out, uint8_t cache[8], const Keyframe& row) {
  RefStep s{};
  s.ticks = row.duration_ms / 10;
  for (int k = 0; k < 8; ++k) {
    s.angle[k] = row.angle[k];
    s.changed[k] = cache[k] != row.angle[k];
    s.lastStockWrite[k] = -1;
    if (s.changed[k] && s.ticks > 0) {
      const int a = cache[k];
      const int b = row.angle[k];
      const int i = (int)s.ticks - 1;
      s.lastStockWrite[k] = a + (b - a) * i / (int)s.ticks;
    }
    cache[k] = row.angle[k];
  }
  out.push_back(s);
}

void stockTable(std::vector<RefStep>& out, uint8_t cache[8], const Keyframe* rows, size_t count) {
  for (size_t r = 0; r < count; ++r) stockRow(out, cache, rows[r]);
}

#define TABLE(t) stock::t, sizeof stock::t / sizeof stock::t[0]

// The stock dispatch per pm value.
std::vector<RefStep> stockTrace(int pm, const uint8_t start[8]) {
  std::vector<RefStep> out;
  uint8_t cache[8];
  for (int k = 0; k < 8; ++k) cache[k] = start[k];
  switch (pm) {
    case 1: stockTable(out, cache, TABLE(kStandby)); break;
    case 2: stockTable(out, cache, TABLE(kForward)); break;
    case 3: stockTable(out, cache, TABLE(kBackward)); break;
    case 4: stockTable(out, cache, TABLE(kLeftShift)); break;
    case 5: stockTable(out, cache, TABLE(kRightShift)); break;
    case 6: stockTable(out, cache, TABLE(kTurnLeft)); break;
    case 7: stockTable(out, cache, TABLE(kTurnRight)); break;
    case 8: stockTable(out, cache, TABLE(kLie)); break;
    case 9:  // 0x5169-0x517A: Say Hi, then Standby
      stockTable(out, cache, TABLE(kSayHi));
      stockTable(out, cache, TABLE(kStandby));
      break;
    case 10: stockTable(out, cache, TABLE(kFighting)); break;
    case 11: stockTable(out, cache, TABLE(kPushUp)); break;
    case 12:  // 0x5191-0x51A2: Standby, then Sleep
      stockTable(out, cache, TABLE(kStandby));
      stockTable(out, cache, TABLE(kSleep));
      break;
    case 13: stockTable(out, cache, TABLE(kDance1)); break;
    case 14: stockTable(out, cache, TABLE(kDance2)); break;
    case 15: stockTable(out, cache, TABLE(kDance3)); break;
    case 99:
    case 100: {
      // Eight writes with delay(10) after each, then delay(300): one step per
      // servo of one tick, then a 30-tick hold that writes nothing.
      const uint8_t* pose = pm == 99 ? stock::kCentrePose : stock::kZeroPose;
      for (int k = 0; k < 8; ++k) {
        RefStep s{};
        s.ticks = 1;
        for (int j = 0; j < 8; ++j) {
          s.angle[j] = j <= k ? pose[j] : cache[j];
          s.changed[j] = j == k;
          s.lastStockWrite[j] = j == k ? pose[j] : -1;
        }
        out.push_back(s);
      }
      RefStep hold{};
      hold.ticks = 30;
      for (int j = 0; j < 8; ++j) {
        hold.angle[j] = pose[j];
        hold.lastStockWrite[j] = -1;
      }
      out.push_back(hold);
      break;
    }
    default: break;
  }
  return out;
}

const struct {
  int id;
  uint32_t ticks;
} kDurationTable[] = {{1, 40},   {2, 165}, {3, 165}, {4, 165}, {5, 165}, {6, 120}, {7, 120}, {8, 50}, {9, 320},
                      {10, 407}, {11, 718}, {12, 114}, {13, 300}, {14, 270}, {15, 300}, {99, 38}, {100, 38}};

void checkMotion(int id, uint32_t expectTicks, const uint8_t start[8]) {
  char msg[48];
  snprintf(msg, sizeof msg, "motion %d", id);
  const std::vector<RefStep> ref = stockTrace(id, start);
  TEST_ASSERT_FALSE_MESSAGE(ref.empty(), msg);
  uint32_t refTotal = 0;
  for (const RefStep& s : ref) refTotal += s.ticks;
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(expectTicks, refTotal, msg);

  rig::Rig r;
  r.m.activeAt(start);
  TEST_ASSERT_TRUE(r.p.request(id, Origin::Check));
  r.at(0);
  r.untilIdle();
  TEST_ASSERT_EQUAL_MESSAGE(1, (int)r.runs.size(), msg);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(expectTicks, r.runs[0].ticks, msg);
  TEST_ASSERT_EQUAL_MESSAGE((int)expectTicks, (int)r.tickTimes.size(), msg);

  // Replay the writes tick by tick, segmented by the reference's keyframes.
  int pose[8];
  for (int k = 0; k < 8; ++k) pose[k] = start[k];
  size_t w = 0;
  size_t tick = 0;
  for (size_t step = 0; step < ref.size(); ++step) {
    const RefStep& s = ref[step];
    snprintf(msg, sizeof msg, "motion %d keyframe %u", id, (unsigned)step + 1);
    for (uint32_t i = 1; i <= s.ticks; ++i, ++tick) {
      const uint32_t t = r.tickTimes[tick];
      bool written[8] = {};
      int lastValue[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
      while (w < r.m.writes.size() && r.m.writes[w].t == t) {
        const rig::Write& x = r.m.writes[w++];
        written[x.servo] = true;
        lastValue[x.servo] = x.angle;
        pose[x.servo] = x.angle;
      }
      for (int k = 0; k < 8; ++k) {
        // A servo that changes is written at every tick of its keyframe, and
        // only there; one that does not is never written.
        const bool expectWrite = id >= 99 ? (s.changed[k] && i == 1) : s.changed[k];
        TEST_ASSERT_EQUAL_MESSAGE(expectWrite, written[k], msg);
        if (i == s.ticks && s.changed[k]) TEST_ASSERT_EQUAL_MESSAGE(s.angle[k], lastValue[k], msg);
      }
    }
    // At each keyframe end, the commanded angles are the keyframe angles.
    for (int k = 0; k < 8; ++k) TEST_ASSERT_EQUAL_MESSAGE(s.angle[k], pose[k], msg);
  }
  TEST_ASSERT_EQUAL_MESSAGE(r.m.writes.size(), w, msg);
}

}  // namespace

static void test_trace_from_zero_pose() {
  for (const auto& d : kDurationTable) checkMotion(d.id, d.ticks, kZero);
}

static void test_trace_from_standby_pose() {
  for (const auto& d : kDurationTable) checkMotion(d.id, d.ticks, kStandbyPose);
}

// The reference reproduces stock's endpoint shortfall: after Standby from the
// zero pose stock's last write to servo 0 is 71, not 70 (design D3).
static void test_reference_shows_stock_shortfall() {
  const std::vector<RefStep> ref = stockTrace(1, kZero);
  TEST_ASSERT_EQUAL(71, ref[1].lastStockWrite[0]);
  TEST_ASSERT_EQUAL(70, ref[1].angle[0]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_trace_from_zero_pose);
  RUN_TEST(test_trace_from_standby_pose);
  RUN_TEST(test_reference_shows_stock_shortfall);
  return UNITY_END();
}
