#include "drive_link/session.h"

#include "system/periodic.h"

namespace ms {
namespace drive_link {

namespace {

bool sameSender(const Sender& a, const Sender& b) { return a.address == b.address && a.port == b.port; }

// Exactly one valid command frame of this protocol version, and nothing else.
bool decodeCommand(const uint8_t* datagram, int length, Frame& f) {
  if (datagram == nullptr || length != COMMAND_FRAME_SIZE) return false;
  Decoder dec;
  for (int i = 0; i < length; i++) dec.push(datagram[i]);
  if (!dec.flush(f) || f.type != FrameType::Command) return false;
  Frame extra;
  if (dec.flush(extra)) return false;
  return dec.validFrames() == 1 && dec.discardedBytes() == 0 && dec.pending() == 0;
}

}  // namespace

void Session::end() {
  session_ = false;
  result_ = Result::None;
  counts_.failsafes++;
}

void Session::start(Sender sender, const Frame& f, uint32_t nowMs) {
  session_ = true;
  sender_ = sender;
  lastSeq_ = f.seq;
  lastMs_ = nowMs;
  lastCounter_ = f.command.counter;  // the baseline: no request
  result_ = Result::None;
  telemetryDue_ = nowMs;  // the first frame goes out in this pass
}

bool Session::judge(uint32_t nowMs) {
  if (session_ && static_cast<uint32_t>(nowMs - lastMs_) > LIVENESS_MS) {
    end();
    return true;
  }
  return false;
}

Outcome Session::judgeDatagram(const uint8_t* datagram, int length, Sender sender, uint32_t nowMs,
                               AcceptResult& r) {
  // Sender before budget (D4): another station's flood cannot use up the
  // locked remote's budget.
  if (session_ && !sameSender(sender, sender_)) return Outcome::OtherSender;

  const uint32_t window = nowMs / WINDOW_MS;
  if (!windowStarted_ || window_ != window) {
    windowStarted_ = true;
    window_ = window;
    windowUsed_ = 0;
  }
  if (windowUsed_ >= WINDOW_BUDGET) return Outcome::RateLimited;
  windowUsed_++;
  r.decoded = true;

  Frame f;
  if (!decodeCommand(datagram, length, f)) return Outcome::Malformed;

  if (!session_) {
    start(sender, f, nowMs);
    r.sessionStarted = true;
    return Outcome::Accepted;
  }

  const uint8_t d = static_cast<uint8_t>(f.seq - lastSeq_);
  if (d == 0 || d >= 128) return Outcome::OutOfSequence;
  if (d > SEQ_WINDOW) {
    end();
    r.jump = true;
    r.sessionEnded = true;
    return Outcome::OutOfSequence;
  }

  if (f.command.counter != lastCounter_) {
    if (f.command.motion == MOTION_STOP) {
      r.request = Request{RequestKind::Stop, 0};
      result_ = Result::StopRequested;
    } else {
      r.request = Request{RequestKind::Play, f.command.motion};
      result_ = Result::Rejected;  // until recordResult() gives the player's answer
    }
  }
  lastSeq_ = f.seq;
  lastMs_ = nowMs;
  lastCounter_ = f.command.counter;
  return Outcome::Accepted;
}

AcceptResult Session::accept(const uint8_t* datagram, int length, Sender sender, uint32_t nowMs) {
  AcceptResult r = {Outcome::Malformed, false, false, false, false, Request{RequestKind::None, 0}};
  r.sessionEnded = judge(nowMs);
  r.outcome = judgeDatagram(datagram, length, sender, nowMs, r);
  switch (r.outcome) {
    case Outcome::Accepted:
      counts_.accepted++;
      break;
    case Outcome::OtherSender:
      counts_.otherSender++;
      break;
    case Outcome::RateLimited:
      counts_.rateLimited++;
      break;
    case Outcome::Malformed:
      counts_.malformed++;
      break;
    case Outcome::OutOfSequence:
      counts_.outOfSequence++;
      break;
  }
  return r;
}

void Session::recordResult(bool accepted) {
  if (session_) result_ = accepted ? Result::Accepted : Result::Rejected;
}

bool Session::telemetryDue(uint32_t nowMs) const { return session_ && isDue(telemetryDue_, nowMs); }

void Session::telemetrySent(uint32_t nowMs) {
  telemetryDue_ = nextDeadline(telemetryDue_, nowMs, TELEMETRY_PERIOD_MS);
  telemetrySeq_++;
}

Telemetry Session::makeTelemetry(uint8_t playing, uint8_t pending, const uint8_t angles[JOINT_COUNT],
                                 uint32_t nowMs) const {
  Telemetry t;
  t.playing = playing;
  t.pending = pending;
  t.counter = lastCounter_;
  t.result = static_cast<uint8_t>(result_);
  t.commandSeq = lastSeq_;
  const uint32_t age = static_cast<uint32_t>(nowMs - lastMs_);
  t.ageMs = age >= AGE_MAX ? AGE_MAX : static_cast<uint8_t>(age);
  for (int i = 0; i < JOINT_COUNT; i++) t.angles[i] = angles[i];
  t.accepted = counts_.accepted;
  t.malformed = counts_.malformed;
  t.outOfSequence = counts_.outOfSequence;
  t.otherSender = counts_.otherSender;
  t.rateLimited = counts_.rateLimited;
  t.failsafes = counts_.failsafes;
  return t;
}

}  // namespace drive_link
}  // namespace ms
