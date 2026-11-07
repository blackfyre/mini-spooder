// The robot side of a drive link session (drive-link, design D1, D3-D6): the
// counterpart of spooder's handheld_controller, with the robot as the endpoint.
// - Every datagram gets one outcome, decided in this order (D4): a lapsed
//   session ends; a datagram from another sender while a session is live is
//   discarded without decoding or charging the budget; a datagram beyond the
//   window's budget (WINDOW_BUDGET per WINDOW_MS, a fixed grid of the clock) is
//   discarded without decoding; then it is validated: exactly one version 1
//   command frame with a valid CRC and nothing else (9 bytes).
// - The first accepted frame starts a session, locks its sender and only sets
//   the counter baseline. Later frames: d = seq - last accepted (mod 256);
//   1..SEQ_WINDOW is newer, 0 or 128..255 is stale, SEQ_WINDOW+1..127 is a jump
//   that ends the session.
// - Each change of the motion-request counter between consecutive accepted
//   frames is one request: motion 0 is Stop, any other id Play(id).
// - Live while the newest accepted frame is no older than LIVENESS_MS; only an
//   accepted frame refreshes it. Every session end (lapse or jump) is one
//   failsafe.
// - Telemetry is due at the session's start and then every TELEMETRY_PERIOD_MS
//   (ms::nextDeadline: no accumulated jitter, no bursts after a late frame).
// All times are millis() values compared by unsigned differences, so they
// survive the wrap. Hardware-free; link.h applies the results in order.
#pragma once

#include <stdint.h>

#include "drive_link/protocol.h"

namespace ms {
namespace drive_link {

constexpr uint16_t PORT = 52041;  // UDP, on the access point's 192.168.4.1
constexpr uint32_t COMMAND_PERIOD_MS = 50;
constexpr uint32_t LIVENESS_MS = 300;
constexpr uint32_t TELEMETRY_PERIOD_MS = 100;
constexpr uint32_t WINDOW_MS = 20;
constexpr int WINDOW_BUDGET = 4;
constexpr uint8_t SEQ_WINDOW = 8;
constexpr int DRAIN_MAX = 128;
// The glue's receive buffer: one byte more than a command frame, so a longer
// datagram is still seen as too long.
constexpr int DATAGRAM_BUFFER = COMMAND_FRAME_SIZE + 1;

struct Sender {
  uint32_t address;  // IPv4, as the socket gives it
  uint16_t port;
};

enum class Outcome : uint8_t {
  Accepted,
  OtherSender,    // while another sender's session is live: not decoded, not charged
  RateLimited,    // beyond the window's budget: not decoded
  Malformed,      // not exactly one valid version 1 command frame
  OutOfSequence,  // stale, repeated, or a jump (see AcceptResult::jump)
};

enum class RequestKind : uint8_t { None, Play, Stop };

struct Request {
  RequestKind kind;
  uint8_t motion;  // for Play
};

struct AcceptResult {
  Outcome outcome;
  bool decoded;         // the datagram reached validation (charged to the budget)
  bool jump;            // out of sequence by a jump, which ended the session
  bool sessionEnded;    // a session ended: a lapse found first, or the jump (a failsafe)
  bool sessionStarted;  // this datagram started a session (after any end above)
  Request request;      // at most one, from an accepted frame of a live session
};

// The counts from boot, wrapping at 65535 as in telemetry.
struct Counts {
  uint16_t accepted;
  uint16_t malformed;
  uint16_t outOfSequence;
  uint16_t otherSender;
  uint16_t rateLimited;
  uint16_t failsafes;
};

class Session {
 public:
  // One datagram of `length` bytes from `sender`, read at `nowMs`. It reads at
  // most COMMAND_FRAME_SIZE bytes of `datagram`, and only when `length` equals
  // that, so the caller may pass the length that the socket reported with a
  // DATAGRAM_BUFFER-byte buffer.
  AcceptResult accept(const uint8_t* datagram, int length, Sender sender, uint32_t nowMs);

  // Ends a session whose newest accepted frame is older than LIVENESS_MS at
  // `nowMs`. True when it ended one (a failsafe).
  bool judge(uint32_t nowMs);

  bool live() const { return session_; }
  Sender sender() const { return sender_; }

  // Motion playback's answer to the Play request that accept() just returned.
  void recordResult(bool accepted);

  // The telemetry schedule of the live session.
  bool telemetryDue(uint32_t nowMs) const;
  void telemetrySent(uint32_t nowMs);
  uint32_t telemetryDeadline() const { return telemetryDue_; }

  // The telemetry payload at `nowMs` from motion playback's state report and
  // servo output's angles (ANGLE_NONE for a released servo).
  Telemetry makeTelemetry(uint8_t playing, uint8_t pending, const uint8_t angles[JOINT_COUNT],
                          uint32_t nowMs) const;
  // The sequence number for the next telemetry frame (one per frame, from boot).
  uint8_t telemetrySeq() const { return telemetrySeq_; }

  const Counts& counts() const { return counts_; }

 private:
  Outcome judgeDatagram(const uint8_t* datagram, int length, Sender sender, uint32_t nowMs,
                        AcceptResult& r);
  void start(Sender sender, const Frame& f, uint32_t nowMs);
  void end();

  // The rate window.
  bool windowStarted_ = false;
  uint32_t window_ = 0;  // nowMs / WINDOW_MS
  int windowUsed_ = 0;
  // The session.
  bool session_ = false;
  Sender sender_ = {0, 0};
  uint8_t lastSeq_ = 0;
  uint32_t lastMs_ = 0;
  uint8_t lastCounter_ = 0;
  Result result_ = Result::None;
  uint32_t telemetryDue_ = 0;
  uint8_t telemetrySeq_ = 0;
  Counts counts_ = {0, 0, 0, 0, 0, 0};
};

}  // namespace drive_link
}  // namespace ms
