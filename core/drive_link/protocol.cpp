#include "drive_link/protocol.h"

namespace ms {
namespace drive_link {

uint16_t crc16(const uint8_t* data, int length) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < length; i++) {
    crc ^= static_cast<uint16_t>(data[i] << 8);
    for (int bit = 0; bit < 8; bit++)
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
  }
  return crc;
}

namespace {

void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>(v >> 8);
}

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

int payloadSize(uint8_t type) {
  switch (static_cast<FrameType>(type)) {
    case FrameType::Command:
      return COMMAND_PAYLOAD_SIZE;
    case FrameType::Telemetry:
      return TELEMETRY_PAYLOAD_SIZE;
  }
  return -1;
}

int finishFrame(FrameType type, uint8_t seq, uint8_t* out) {
  const int n = payloadSize(static_cast<uint8_t>(type));
  out[0] = START;
  out[1] = VERSION;
  out[2] = static_cast<uint8_t>(type);
  out[3] = seq;
  out[4] = static_cast<uint8_t>(n);
  put16(out + HEADER_SIZE + n, crc16(out + 1, HEADER_SIZE - 1 + n));
  return HEADER_SIZE + n + CRC_SIZE;
}

void parseCommand(const uint8_t* p, Command& c) {
  c.motion = p[0];
  c.counter = p[1];
}

void parseTelemetry(const uint8_t* p, Telemetry& t) {
  t.playing = p[0];
  t.pending = p[1];
  t.counter = p[2];
  t.result = p[3];
  t.commandSeq = p[4];
  t.ageMs = p[5];
  for (int i = 0; i < JOINT_COUNT; i++) t.angles[i] = p[6 + i];
  t.accepted = get16(p + 14);
  t.malformed = get16(p + 16);
  t.outOfSequence = get16(p + 18);
  t.otherSender = get16(p + 20);
  t.rateLimited = get16(p + 22);
  t.failsafes = get16(p + 24);
}

}  // namespace

int encodeCommand(const Command& c, uint8_t seq, uint8_t* out) {
  uint8_t* p = out + HEADER_SIZE;
  p[0] = c.motion;
  p[1] = c.counter;
  return finishFrame(FrameType::Command, seq, out);
}

int encodeTelemetry(const Telemetry& t, uint8_t seq, uint8_t* out) {
  uint8_t* p = out + HEADER_SIZE;
  p[0] = t.playing;
  p[1] = t.pending;
  p[2] = t.counter;
  p[3] = t.result;
  p[4] = t.commandSeq;
  p[5] = t.ageMs;
  for (int i = 0; i < JOINT_COUNT; i++) p[6 + i] = t.angles[i];
  put16(p + 14, t.accepted);
  put16(p + 16, t.malformed);
  put16(p + 18, t.outOfSequence);
  put16(p + 20, t.otherSender);
  put16(p + 22, t.rateLimited);
  put16(p + 24, t.failsafes);
  return finishFrame(FrameType::Telemetry, seq, out);
}

void Decoder::push(uint8_t byte) {
  // next() keeps the buffer shorter than a frame; if the caller skipped it,
  // make room by giving up the oldest candidate.
  if (len_ == MAX_FRAME_SIZE) reject();
  if (len_ == 0 && byte != START) {
    discardedBytes_++;  // between frames
    return;
  }
  buf_[len_++] = byte;
}

void Decoder::drop(int count) {
  for (int i = count; i < len_; i++) buf_[i - count] = buf_[i];
  len_ -= count;
  discardedBytes_ += static_cast<uint32_t>(count);
}

void Decoder::reject() {
  discarded_++;
  drop(1);  // resume the search just after this candidate's start marker
}

// Checks the candidate at buf_[0]: Invalid, Incomplete, or a complete CRC-valid
// frame of *total bytes.
Decoder::Candidate Decoder::check(int* total) const {
  const uint8_t* b = buf_;
  const int len = len_;
  if (b[0] != START) return Candidate::Invalid;
  if (len >= 2 && b[1] != VERSION) return Candidate::Invalid;
  const int n = len >= 3 ? payloadSize(b[2]) : 0;
  if (n < 0 || (len >= 5 && b[4] != n)) return Candidate::Invalid;
  *total = HEADER_SIZE + n + CRC_SIZE;
  if (len < HEADER_SIZE || len < *total) return Candidate::Incomplete;
  if (crc16(b + 1, HEADER_SIZE - 1 + n) != get16(b + HEADER_SIZE + n)) return Candidate::Invalid;
  return Candidate::Valid;
}

void Decoder::emit(int total, Frame& out) {
  out.type = static_cast<FrameType>(buf_[2]);
  out.seq = buf_[3];
  const uint8_t* payload = buf_ + HEADER_SIZE;
  switch (out.type) {
    case FrameType::Command:
      parseCommand(payload, out.command);
      break;
    case FrameType::Telemetry:
      parseTelemetry(payload, out.telemetry);
      break;
  }
  valid_++;
  for (int i = total; i < len_; i++) buf_[i - total] = buf_[i];
  len_ -= total;
}

void Decoder::idle() {
  int total = 0;
  if (len_ > 0 && check(&total) == Candidate::Incomplete) reject();  // next() rescans the rest
}

bool Decoder::flush(Frame& out) {
  for (;;) {
    if (next(out)) return true;
    if (len_ == 0) return false;
    idle();
  }
}

bool Decoder::next(Frame& out) {
  while (len_ > 0) {
    int total = 0;
    const Candidate c = check(&total);
    if (c == Candidate::Valid) {
      emit(total, out);
      return true;
    }
    if (c == Candidate::Invalid) {
      if (buf_[0] == START)
        reject();
      else
        drop(1);
      continue;
    }
    // Incomplete: keep the earliest candidate until its CRC can be checked, or
    // until idle() abandons it.
    return false;
  }
  return false;
}

}  // namespace drive_link
}  // namespace ms
