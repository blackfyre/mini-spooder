// The motion-demo sequence (motion-playback D9), header-only so that it is
// compiled only where it is included: the motion-demo build's glue and the host
// log generator (core/fuzz/motion_demo_log/). The normal image never includes it.
//
// After go: motions 1-15, 99 and 100 in turn with a 2 s pause after each; then
// Forward, stopped 500 ms after its start; then Dance 1 with motion 1 requested
// 1 s after its start; then one 500 ms keyframe. Every request has origin
// `check`. One line per run, per command and for the keyframe:
//   motion <id> start <ms> last_write <ms> end <ms> ticks <n> <complete|interrupted>
//   command <stop|request <id>> <ms>
//   keyframe start <ms> end <ms> ticks <n>
#pragma once

#include <stdint.h>
#include <stdio.h>

#include "motion/player.h"
#include "system/periodic.h"

namespace ms {

class MotionDemo {
 public:
  using LogFn = void (*)(void* ctx, const char* line);

  static constexpr uint32_t kPauseMs = 2000;
  static constexpr uint32_t kStopAfterMs = 500;
  static constexpr uint32_t kRequestAfterMs = 1000;
  static constexpr int32_t kKeyframeMs = 500;

  MotionDemo(MotionPlayer& player, LogFn log, void* ctx) : player_(player), log_(log), ctx_(ctx) {
    player_.setRunListener(&MotionDemo::onRun, this);
  }

  // Starts the sequence (the `go` line). Ignored once started.
  void go(uint32_t nowMs) {
    if (phase_ != Phase::WaitGo) return;
    char line[32];
    snprintf(line, sizeof line, "demo go %lu", (unsigned long)nowMs);
    log_(ctx_, line);
    phase_ = Phase::Request;
  }

  // Called after the player's service() in the same loop pass.
  void poll(uint32_t nowMs) {
    switch (phase_) {
      case Phase::WaitGo:
      case Phase::Done:
        return;
      case Phase::Request:
        if (index_ < kListCount) {
          player_.request(kList[index_++], Origin::Check);
          waitThen(Phase::Request);
        } else {
          player_.request(2, Origin::Check);
          phase_ = Phase::ForwardRunning;
        }
        return;
      case Phase::WaitIdle:
        if (idle()) {
          untilMs_ = nowMs + kPauseMs;
          phase_ = Phase::Pause;
        }
        return;
      case Phase::Pause:
        if (isDue(untilMs_, nowMs)) phase_ = afterPause_;
        return;
      case Phase::ForwardRunning: {
        const RunRecord* run = player_.currentRun();
        if (run != nullptr && run->motion == 2 && isDue(run->startMs + kStopAfterMs, nowMs)) {
          player_.stop();
          command("stop", nowMs);
          waitThen(Phase::DanceRequest);
        } else if (idle()) {
          waitThen(Phase::DanceRequest);  // ended without the stop (refused)
        }
        return;
      }
      case Phase::DanceRequest:
        player_.request(13, Origin::Check);
        phase_ = Phase::DanceRunning;
        return;
      case Phase::DanceRunning: {
        const RunRecord* run = player_.currentRun();
        if (run != nullptr && run->motion == 13 && isDue(run->startMs + kRequestAfterMs, nowMs)) {
          player_.request(1, Origin::Check);
          command("request 1", nowMs);
          waitThen(Phase::KeyframeRequest);
        } else if (idle()) {
          waitThen(Phase::KeyframeRequest);
        }
        return;
      }
      case Phase::KeyframeRequest: {
        const int pose[8] = {70, 90, 90, 110, 110, 90, 90, 70};
        player_.playKeyframe(pose, kKeyframeMs, Origin::Check);
        phase_ = Phase::KeyframeRunning;
        return;
      }
      case Phase::KeyframeRunning:
        if (idle()) {
          char line[48];
          snprintf(line, sizeof line, "demo done tick_max_us %lu", (unsigned long)tickMaxUs_);
          log_(ctx_, line);
          phase_ = Phase::Done;
        }
        return;
    }
  }

  // The processor time of one service() call, for the maximum (task 3.2).
  void noteServiceUs(uint32_t us) {
    if (us > tickMaxUs_) tickMaxUs_ = us;
  }

  bool done() const { return phase_ == Phase::Done; }

 private:
  enum class Phase : uint8_t {
    WaitGo,
    Request,
    WaitIdle,
    Pause,
    ForwardRunning,
    DanceRequest,
    DanceRunning,
    KeyframeRequest,
    KeyframeRunning,
    Done,
  };

  static constexpr int kList[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 99, 100};
  static constexpr int kListCount = sizeof kList / sizeof kList[0];

  bool idle() const {
    const PlayerState s = player_.state();
    return s.activity == PlayerActivity::Idle && s.pendingMotion == 0 && s.pendingKeyframe == 0;
  }

  void waitThen(Phase next) {
    afterPause_ = next;
    phase_ = Phase::WaitIdle;
  }

  void command(const char* what, uint32_t nowMs) {
    char line[40];
    snprintf(line, sizeof line, "command %s %lu", what, (unsigned long)nowMs);
    log_(ctx_, line);
  }

  static void onRun(void* ctx, const RunRecord& run) {
    MotionDemo* self = static_cast<MotionDemo*>(ctx);
    char line[112];
    if (run.kind == PlayerActivity::Keyframe) {
      snprintf(line, sizeof line, "keyframe start %lu end %lu ticks %lu", (unsigned long)run.startMs,
               (unsigned long)run.endMs, (unsigned long)run.ticks);
    } else {
      snprintf(line, sizeof line, "motion %u start %lu last_write %lu end %lu ticks %lu %s", run.motion,
               (unsigned long)run.startMs, (unsigned long)(run.wrote ? run.lastWriteMs : run.startMs),
               (unsigned long)run.endMs, (unsigned long)run.ticks, run.complete ? "complete" : "interrupted");
    }
    self->log_(self->ctx_, line);
  }

  MotionPlayer& player_;
  LogFn log_;
  void* ctx_;
  Phase phase_ = Phase::WaitGo;
  Phase afterPause_ = Phase::Request;
  int index_ = 0;
  uint32_t untilMs_ = 0;
  uint32_t tickMaxUs_ = 0;
};

}  // namespace ms
