#include "motion/player.h"

#include "motion/stock_motions.h"
#include "system/periodic.h"

namespace ms {

namespace {

constexpr int kServos = 8;

}  // namespace

MotionPlayer::MotionPlayer(MotionOutput& output)
    : out_(output),
      listener_(nullptr),
      listenerCtx_(nullptr),
      current_(),
      pending_(),
      run_(),
      nextKeyframeId_(0),
      lastNowMs_(0),
      tickCount_(0),
      dueMs_(0),
      finished_(false),
      table_(0),
      frame_(0),
      tick_(1),
      ticks_(1),
      start_(),
      step_(0) {
  current_.kind = PlayerActivity::Idle;
  pending_.kind = PlayerActivity::Idle;
}

void MotionPlayer::setRunListener(RunListener listener, void* ctx) {
  listener_ = listener;
  listenerCtx_ = ctx;
}

bool MotionPlayer::request(int id, Origin origin) {
  const Motion* motion = findMotion(id);
  if (motion == nullptr) return false;
  pending_ = Job{PlayerActivity::Motion, motion, 0, Keyframe{}, origin};
  return true;
}

uint32_t MotionPlayer::playKeyframe(const int angles[8], int32_t durationMs, Origin origin) {
  if (angles == nullptr || durationMs < 0 || durationMs > kKeyframeMaxMs) return 0;
  Keyframe frame{};
  for (int s = 0; s < kServos; ++s) {
    if (angles[s] < 0 || angles[s] > 180) return 0;
    frame.angle[s] = static_cast<uint8_t>(angles[s]);
  }
  frame.duration_ms = static_cast<uint16_t>(durationMs);
  ++nextKeyframeId_;
  if (nextKeyframeId_ == 0) ++nextKeyframeId_;
  pending_ = Job{PlayerActivity::Keyframe, nullptr, nextKeyframeId_, frame, origin};
  return nextKeyframeId_;
}

bool MotionPlayer::keyframeEnded(uint32_t id) const {
  if (id == 0) return true;
  if (current_.kind == PlayerActivity::Keyframe && current_.keyframeId == id) return false;
  if (pending_.kind == PlayerActivity::Keyframe && pending_.keyframeId == id) return false;
  return true;
}

void MotionPlayer::stop() {
  pending_.kind = PlayerActivity::Idle;
  if (current_.kind != PlayerActivity::Idle) endRun(false, lastNowMs_);
}

PlayerState MotionPlayer::state() const {
  PlayerState s{};
  s.activity = current_.kind;
  if (current_.kind == PlayerActivity::Motion) s.motion = current_.motion->id;
  if (current_.kind == PlayerActivity::Keyframe) s.keyframe = current_.keyframeId;
  if (pending_.kind == PlayerActivity::Motion) s.pendingMotion = pending_.motion->id;
  if (pending_.kind == PlayerActivity::Keyframe) s.pendingKeyframe = pending_.keyframeId;
  return s;
}

const Keyframe& MotionPlayer::frameAt(uint8_t table, uint8_t index) const {
  if (current_.kind == PlayerActivity::Keyframe) return current_.frame;
  return current_.motion->tables[table].frames[index];
}

uint8_t MotionPlayer::frameCount(uint8_t table) const {
  if (current_.kind == PlayerActivity::Keyframe) return table == 0 ? 1 : 0;
  return table < current_.motion->table_count ? current_.motion->tables[table].count : 0;
}

void MotionPlayer::service(uint32_t nowMs) {
  lastNowMs_ = nowMs;
  if (current_.kind != PlayerActivity::Idle) {
    // The mode first, at every call, due tick or not (D11).
    if (!out_.active()) {
      abandon(nowMs);
      return;
    }
    if (!isDue(dueMs_, nowMs)) return;
    if (!finished_) {
      playTick(dueMs_, nowMs);
      return;
    }
    const uint32_t dueMs = dueMs_;
    endRun(true, nowMs);
    if (pending_.kind == PlayerActivity::Idle) return;
    // From the pending slot: no enable request, first tick at this chance.
    const Job next = pending_;
    pending_.kind = PlayerActivity::Idle;
    startRun(next, nowMs);
    playTick(dueMs, nowMs);
    return;
  }
  if (pending_.kind == PlayerActivity::Idle) return;
  // Accepted while idle: the first tick sends the enable request.
  const Job next = pending_;
  pending_.kind = PlayerActivity::Idle;
  startRun(next, nowMs);
  enableTick(nowMs);
}

void MotionPlayer::startRun(const Job& job, uint32_t nowMs) {
  current_ = job;
  run_ = RunRecord{};
  run_.kind = job.kind;
  run_.motion = job.kind == PlayerActivity::Motion ? job.motion->id : 0;
  run_.keyframe = job.keyframeId;
  run_.startMs = nowMs;
  finished_ = false;
  table_ = 0;
  frame_ = 0;
  tick_ = 1;
  ticks_ = 1;
  step_ = 0;
}

void MotionPlayer::enableTick(uint32_t nowMs) {
  uint8_t pose[kServos];
  if (!out_.active()) {
    // The zero pose in a tick of its own; the motion starts at the next tick.
    for (int s = 0; s < kServos; ++s) pose[s] = stock::kZeroPose[s];
    ++run_.ticks;
    ++tickCount_;
    if (!out_.enable(current_.origin, pose)) {
      abandon(nowMs);
      return;
    }
    dueMs_ = nextDeadline(nowMs, nowMs, kTickMs);
    return;
  }
  // Output active: the commanded angles (nothing moves), then the first tick.
  for (int s = 0; s < kServos; ++s) pose[s] = static_cast<uint8_t>(out_.commandedAngle(s));
  if (!out_.enable(current_.origin, pose)) {
    ++run_.ticks;
    ++tickCount_;
    abandon(nowMs);
    return;
  }
  playTick(nowMs, nowMs);
}

bool MotionPlayer::playTick(uint32_t dueMs, uint32_t nowMs) {
  ++run_.ticks;
  ++tickCount_;
  bool wrote = false;
  if (current_.kind == PlayerActivity::Motion && current_.motion->direct_pose != nullptr) {
    if (step_ < kServos) {
      if (!out_.write(step_, current_.motion->direct_pose[step_])) {
        abandon(nowMs);
        return false;
      }
      wrote = true;
    }
    ++step_;
    if (step_ >= kDirectPoseTicks) finished_ = true;
  } else {
    const Keyframe& frame = frameAt(table_, frame_);
    if (tick_ == 1) {
      ticks_ = keyframeTicks(frame.duration_ms);
      for (int s = 0; s < kServos; ++s) start_[s] = static_cast<uint8_t>(out_.commandedAngle(s));
    }
    for (int s = 0; s < kServos; ++s) {
      const int a = start_[s];
      const int b = frame.angle[s];
      if (a == b) continue;
      const uint32_t span = static_cast<uint32_t>(b > a ? b - a : a - b);
      const int d = static_cast<int>((tick_ * span + ticks_ / 2) / ticks_);
      if (!out_.write(s, b > a ? a + d : a - d)) {
        abandon(nowMs);
        return false;
      }
      wrote = true;
    }
    if (++tick_ > ticks_) {
      tick_ = 1;
      if (++frame_ >= frameCount(table_)) {
        frame_ = 0;
        if (++table_ >= (current_.kind == PlayerActivity::Keyframe ? 1 : current_.motion->table_count)) finished_ = true;
      }
    }
  }
  if (wrote) {
    run_.wrote = true;
    run_.lastWriteMs = nowMs;
  }
  dueMs_ = nextDeadline(dueMs, nowMs, kTickMs);
  return true;
}

void MotionPlayer::abandon(uint32_t nowMs) {
  pending_.kind = PlayerActivity::Idle;
  endRun(false, nowMs);
}

void MotionPlayer::endRun(bool complete, uint32_t nowMs) {
  if (current_.kind == PlayerActivity::Idle) return;
  run_.endMs = nowMs;
  run_.complete = complete;
  current_.kind = PlayerActivity::Idle;
  if (listener_ != nullptr) listener_(listenerCtx_, run_);
}

}  // namespace ms
