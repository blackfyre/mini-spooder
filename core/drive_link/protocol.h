// Drive link protocol version 1 (drive-link, design D2): frame layout, CRC,
// encoders and an incremental decoder. Copied from spooder's core/protocol and
// reduced to two frame types. core/test/golden/drive_link_v1.txt pins the
// encoding for the pak's Go copy. Hardware-free: no Arduino headers.
//
// Frame:
//   0      start marker 0xA5
//   1      protocol version (1)
//   2      type (1 = command, 2 = telemetry)
//   3      sequence number (wraps)
//   4      payload length N (2 for command, 26 for telemetry)
//   5      payload, N bytes, little-endian
//   5+N    CRC-16/CCITT-FALSE over bytes 1..4+N, little-endian
#pragma once

#include <stdint.h>

namespace ms {
namespace drive_link {

constexpr uint8_t START = 0xA5;
constexpr uint8_t VERSION = 1;

enum class FrameType : uint8_t {
  Command = 1,
  Telemetry = 2,
};

constexpr int HEADER_SIZE = 5;
constexpr int CRC_SIZE = 2;
constexpr int COMMAND_PAYLOAD_SIZE = 2;
constexpr int TELEMETRY_PAYLOAD_SIZE = 26;
constexpr int COMMAND_FRAME_SIZE = HEADER_SIZE + COMMAND_PAYLOAD_SIZE + CRC_SIZE;      // 9
constexpr int TELEMETRY_FRAME_SIZE = HEADER_SIZE + TELEMETRY_PAYLOAD_SIZE + CRC_SIZE;  // 33
constexpr int MAX_FRAME_SIZE = TELEMETRY_FRAME_SIZE;
constexpr int JOINT_COUNT = 8;

// Motion id 0 in a command frame: a stop request.
constexpr uint8_t MOTION_STOP = 0;
// A joint angle for a servo that is released or has no commanded angle.
constexpr uint8_t ANGLE_NONE = 255;
// The age field's value for 255 ms or more.
constexpr uint8_t AGE_MAX = 255;

// Remote to robot, every 50 ms: the complete desired state, never events.
struct Command {
  uint8_t motion;   // requested motion: 0 = stop, else a motion playback id
  uint8_t counter;  // motion-request counter, advanced once per explicit request
};

// The result of the last request processed in a session.
enum class Result : uint8_t {
  None = 0,           // none yet in this session
  Accepted = 1,       // motion accepted by motion playback
  StopRequested = 2,  // stop requested
  Rejected = 3,       // motion rejected by motion playback
};

// Robot to the locked remote, every 100 ms while a session is live.
struct Telemetry {
  uint8_t playing;     // motion playing, 0 when none
  uint8_t pending;     // motion pending behind it, 0 when none
  uint8_t counter;     // counter of the last request processed, or the baseline
  uint8_t result;      // Result value
  uint8_t commandSeq;  // sequence number of the newest accepted command frame
  uint8_t ageMs;       // its age when this frame was built, 255 for 255 ms or more
  uint8_t angles[JOINT_COUNT];  // commanded angles, ANGLE_NONE when released
  uint16_t accepted;
  uint16_t malformed;
  uint16_t outOfSequence;
  uint16_t otherSender;
  uint16_t rateLimited;
  uint16_t failsafes;
};

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no final XOR).
uint16_t crc16(const uint8_t* data, int length);

// Write a complete frame into `out` (at least the frame's size); return its size.
int encodeCommand(const Command& c, uint8_t seq, uint8_t* out);
int encodeTelemetry(const Telemetry& t, uint8_t seq, uint8_t* out);

struct Frame {
  FrameType type;
  uint8_t seq;
  Command command;      // valid when type == Command
  Telemetry telemetry;  // valid when type == Telemetry
};

// Incremental decoder. Feed every received byte with push(), and after each one
// call next() until it returns false: the buffer holds a single frame of at most
// MAX_FRAME_SIZE bytes, so bytes pushed beyond that without next() give up the
// oldest candidate. A rejected candidate (bad version, type, length or CRC) is
// counted, and decoding resumes at the next start marker after its first byte, so
// a valid frame following garbage is never lost. An incomplete candidate is kept
// until its CRC can be checked; when the input ends (the end of a datagram), call
// idle() and then next() until it returns false, and repeat both while pending()
// is not 0, so a false start cannot hide a complete frame behind it. Each idle()
// gives up at least one byte, so the repetition ends. flush() does exactly that.
class Decoder {
 public:
  void push(uint8_t byte);
  bool next(Frame& out);
  void idle();
  // After the input has ended: the next frame, idling past incomplete candidates;
  // false once nothing is held.
  bool flush(Frame& out);

  uint32_t validFrames() const { return valid_; }
  uint32_t discardedFrames() const { return discarded_; }
  // Input bytes given up: between frames, or the first byte of a rejected
  // candidate. Every pushed byte ends up either here, in a returned frame, or
  // still pending.
  uint32_t discardedBytes() const { return discardedBytes_; }
  // Bytes held for a frame not yet complete (0 between frames).
  int pending() const { return len_; }

 private:
  enum class Candidate { Invalid, Incomplete, Valid };
  Candidate check(int* total) const;
  void emit(int total, Frame& out);
  void drop(int count);
  void reject();

  uint8_t buf_[MAX_FRAME_SIZE] = {};
  int len_ = 0;
  uint32_t valid_ = 0;
  uint32_t discarded_ = 0;
  uint32_t discardedBytes_ = 0;
};

}  // namespace drive_link
}  // namespace ms
