// The stock motion catalogue over the generated tables (motion-playback D2).
#include "motion/keyframe.h"
#include "motion/stock_motions.h"

namespace ms {
namespace {

template <size_t N>
constexpr MotionTable table(const Keyframe (&frames)[N]) {
  static_assert(N > 0 && N < 256, "table size");
  return MotionTable{frames, static_cast<uint8_t>(N)};
}

constexpr MotionTable kNone = {nullptr, 0};

const Motion kMotions[kMotionCount] = {
    {1, "Standby", 1, {table(stock::kStandby), kNone}, nullptr},
    {2, "Forward", 1, {table(stock::kForward), kNone}, nullptr},
    {3, "Backward", 1, {table(stock::kBackward), kNone}, nullptr},
    {4, "Left shift", 1, {table(stock::kLeftShift), kNone}, nullptr},
    {5, "Right shift", 1, {table(stock::kRightShift), kNone}, nullptr},
    {6, "Turn left", 1, {table(stock::kTurnLeft), kNone}, nullptr},
    {7, "Turn right", 1, {table(stock::kTurnRight), kNone}, nullptr},
    {8, "Lie", 1, {table(stock::kLie), kNone}, nullptr},
    // Stock dispatch 0x5169-0x517A: Say Hi, then Standby.
    {9, "Say Hi", 2, {table(stock::kSayHi), table(stock::kStandby)}, nullptr},
    {10, "Fighting", 1, {table(stock::kFighting), kNone}, nullptr},
    {11, "Push up", 1, {table(stock::kPushUp), kNone}, nullptr},
    // Stock dispatch 0x5191-0x51A2: Standby, then Sleep.
    {12, "Sleep", 2, {table(stock::kStandby), table(stock::kSleep)}, nullptr},
    {13, "Dance 1", 1, {table(stock::kDance1), kNone}, nullptr},
    {14, "Dance 2", 1, {table(stock::kDance2), kNone}, nullptr},
    {15, "Dance 3", 1, {table(stock::kDance3), kNone}, nullptr},
    {99, "Centre", 0, {kNone, kNone}, stock::kCentrePose},
    {100, "Zero position", 0, {kNone, kNone}, stock::kZeroPose},
};

}  // namespace

const Motion* motions() { return kMotions; }

const Motion* findMotion(int id) {
  for (const Motion& m : kMotions) {
    if (m.id == id) return &m;
  }
  return nullptr;
}

uint32_t motionTicks(const Motion& motion) {
  if (motion.direct_pose != nullptr) return kDirectPoseTicks;
  uint32_t ticks = 0;
  for (int t = 0; t < motion.table_count; ++t) {
    for (int k = 0; k < motion.tables[t].count; ++k) ticks += keyframeTicks(motion.tables[t].frames[k].duration_ms);
  }
  return ticks;
}

}  // namespace ms
