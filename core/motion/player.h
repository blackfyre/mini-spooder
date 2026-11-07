// The non-blocking, tick-driven motion player (motion-playback D3-D6, D10, D11).
// Hardware-free: it reaches servo output only through MotionOutput.
#pragma once

#include <stdint.h>

#include "motion/keyframe.h"
#include "servo/output_gate.h"

namespace ms {

// The player's view of servo output (D4). The firmware implements it on the
// output gate (GateMotionOutput, which keeps the writer token); the host tests
// implement it with a recording model.
class MotionOutput {
 public:
  // An enable request with a complete pose; true when accepted.
  virtual bool enable(Origin origin, const uint8_t angles[8]) = 0;
  // A single-servo write; true when accepted.
  virtual bool write(int servo, int angle) = 0;
  // Whether servo output's mode is active.
  virtual bool active() const = 0;
  // The servo's last commanded angle, before calibration.
  virtual int commandedAngle(int servo) const = 0;

 protected:
  ~MotionOutput() = default;
};

enum class PlayerActivity : uint8_t { Idle, Motion, Keyframe };

// The state report: ids 0 and identifiers 0 mean none.
struct PlayerState {
  PlayerActivity activity;
  uint8_t motion;            // current motion, 0 when idle or playing a keyframe
  uint32_t keyframe;         // current keyframe identifier
  uint8_t pendingMotion;
  uint32_t pendingKeyframe;
};

// One run of a motion or keyframe, from its first tick (an enable tick
// included) to its end. Reported to the run listener when the run ends.
struct RunRecord {
  PlayerActivity kind;  // Motion or Keyframe
  uint8_t motion;
  uint32_t keyframe;
  uint32_t startMs;      // time of the first tick
  uint32_t lastWriteMs;  // time of the last tick that wrote an angle (valid when wrote)
  bool wrote;
  uint32_t endMs;        // time the run ended
  uint32_t ticks;        // ticks run, the enable tick included
  bool complete;         // false when stopped, ended by the mode or by a refusal
};

class MotionPlayer {
 public:
  static constexpr uint32_t kTickMs = 10;
  static constexpr int32_t kKeyframeMaxMs = 60000;

  using RunListener = void (*)(void* ctx, const RunRecord& run);

  explicit MotionPlayer(MotionOutput& output);

  // Called within service() or stop() when a run ends.
  void setRunListener(RunListener listener, void* ctx);

  // Accepts a catalogue id (1-15, 99, 100) into the pending slot; any other id
  // is rejected and changes nothing.
  bool request(int id, Origin origin);

  // Accepts a keyframe of angles 0-180 and a duration 0-60000 ms into the
  // pending slot and returns its identifier; returns 0 (rejected, nothing
  // changed) for anything else.
  uint32_t playKeyframe(const int angles[8], int32_t durationMs, Origin origin);

  // False only for the identifier of the keyframe now playing or pending.
  bool keyframeEnded(uint32_t id) const;

  // Ends the current motion or keyframe and discards the pending request.
  void stop();

  // Runs at most one tick when one is due. Called on every loop pass.
  void service(uint32_t nowMs);

  PlayerState state() const;

  // Ticks run since the reset, enable ticks included.
  uint32_t tickCount() const { return tickCount_; }

  // The run under way, or nullptr when idle.
  const RunRecord* currentRun() const { return current_.kind == PlayerActivity::Idle ? nullptr : &run_; }

 private:
  struct Job {
    PlayerActivity kind;  // Idle means none
    const Motion* motion;
    uint32_t keyframeId;
    Keyframe frame;  // the single keyframe
    Origin origin;
  };

  const Keyframe& frameAt(uint8_t table, uint8_t index) const;
  uint8_t frameCount(uint8_t table) const;
  void startRun(const Job& job, uint32_t nowMs);
  void enableTick(uint32_t nowMs);
  bool playTick(uint32_t dueMs, uint32_t nowMs);
  void endRun(bool complete, uint32_t nowMs);
  void abandon(uint32_t nowMs);

  MotionOutput& out_;
  RunListener listener_;
  void* listenerCtx_;

  Job current_;
  Job pending_;
  RunRecord run_;
  uint32_t nextKeyframeId_;
  uint32_t lastNowMs_;
  uint32_t tickCount_;

  // The cursor of the current run.
  uint32_t dueMs_;
  bool finished_;    // every tick has run; the run ends at the next due chance
  uint8_t table_;    // table index
  uint8_t frame_;    // keyframe index within the table
  uint32_t tick_;    // 1 ... ticks_ within the keyframe
  uint32_t ticks_;   // n of the keyframe
  uint8_t start_[8];
  uint8_t step_;     // direct-pose routine: 0 ... kDirectPoseTicks
};

}  // namespace ms
