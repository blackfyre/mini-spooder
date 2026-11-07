// Drive link protocol version 1 codec (drive-link: Frame format).
#include <string.h>
#include <unity.h>

#include "drive_link/protocol.h"

using namespace ms::drive_link;

void setUp() {}
void tearDown() {}

namespace {

Telemetry sampleTelemetry() {
  Telemetry t = {};
  t.playing = 9;
  t.pending = 3;
  t.counter = 7;
  t.result = 1;
  t.commandSeq = 40;
  t.ageMs = 12;
  const uint8_t angles[JOINT_COUNT] = {70, 90, 90, 110, 110, 90, 90, 255};
  memcpy(t.angles, angles, sizeof angles);
  t.accepted = 1000;
  t.malformed = 2;
  t.outOfSequence = 3;
  t.otherSender = 4;
  t.rateLimited = 5;
  t.failsafes = 65535;
  return t;
}

// Pushes bytes, polling after each, then ends the input; collects the frames.
int decodeStream(Decoder& d, const uint8_t* bytes, int n, Frame* out, int max) {
  int frames = 0;
  Frame f;
  for (int i = 0; i < n; i++) {
    d.push(bytes[i]);
    TEST_ASSERT_TRUE(d.pending() <= MAX_FRAME_SIZE);
    while (d.next(f)) {
      if (frames < max) out[frames] = f;
      frames++;
    }
  }
  while (d.flush(f)) {
    if (frames < max) out[frames] = f;
    frames++;
  }
  return frames;
}

void assertTelemetryEqual(const Telemetry& a, const Telemetry& b) {
  TEST_ASSERT_EQUAL_UINT8(a.playing, b.playing);
  TEST_ASSERT_EQUAL_UINT8(a.pending, b.pending);
  TEST_ASSERT_EQUAL_UINT8(a.counter, b.counter);
  TEST_ASSERT_EQUAL_UINT8(a.result, b.result);
  TEST_ASSERT_EQUAL_UINT8(a.commandSeq, b.commandSeq);
  TEST_ASSERT_EQUAL_UINT8(a.ageMs, b.ageMs);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(a.angles, b.angles, JOINT_COUNT);
  TEST_ASSERT_EQUAL_UINT16(a.accepted, b.accepted);
  TEST_ASSERT_EQUAL_UINT16(a.malformed, b.malformed);
  TEST_ASSERT_EQUAL_UINT16(a.outOfSequence, b.outOfSequence);
  TEST_ASSERT_EQUAL_UINT16(a.otherSender, b.otherSender);
  TEST_ASSERT_EQUAL_UINT16(a.rateLimited, b.rateLimited);
  TEST_ASSERT_EQUAL_UINT16(a.failsafes, b.failsafes);
}

}  // namespace

void test_crc16_check_value() {
  const uint8_t text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16(text, sizeof text));
}

void test_sizes() {
  TEST_ASSERT_EQUAL(9, COMMAND_FRAME_SIZE);
  TEST_ASSERT_EQUAL(33, TELEMETRY_FRAME_SIZE);
  TEST_ASSERT_EQUAL(33, MAX_FRAME_SIZE);
}

void test_command_round_trip_seq_0_and_255() {
  const uint8_t seqs[] = {0, 255};
  for (uint8_t seq : seqs) {
    uint8_t buf[MAX_FRAME_SIZE];
    const Command c = {2, 5};
    const int n = encodeCommand(c, seq, buf);
    TEST_ASSERT_EQUAL(COMMAND_FRAME_SIZE, n);
    TEST_ASSERT_EQUAL_HEX8(0xA5, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(1, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(1, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(seq, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(2, buf[4]);
    Decoder d;
    Frame f[2];
    TEST_ASSERT_EQUAL(1, decodeStream(d, buf, n, f, 2));
    TEST_ASSERT_EQUAL(FrameType::Command, f[0].type);
    TEST_ASSERT_EQUAL_UINT8(seq, f[0].seq);
    TEST_ASSERT_EQUAL_UINT8(2, f[0].command.motion);
    TEST_ASSERT_EQUAL_UINT8(5, f[0].command.counter);
  }
}

void test_telemetry_round_trip_seq_0_and_255() {
  const uint8_t seqs[] = {0, 255};
  for (uint8_t seq : seqs) {
    uint8_t buf[MAX_FRAME_SIZE];
    const Telemetry t = sampleTelemetry();
    const int n = encodeTelemetry(t, seq, buf);
    TEST_ASSERT_EQUAL(TELEMETRY_FRAME_SIZE, n);
    TEST_ASSERT_EQUAL_HEX8(2, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(26, buf[4]);
    Decoder d;
    Frame f[2];
    TEST_ASSERT_EQUAL(1, decodeStream(d, buf, n, f, 2));
    TEST_ASSERT_EQUAL(FrameType::Telemetry, f[0].type);
    TEST_ASSERT_EQUAL_UINT8(seq, f[0].seq);
    assertTelemetryEqual(t, f[0].telemetry);
  }
}

// Scenario: Corrupted frame.
void test_corrupted_frame_then_valid() {
  uint8_t buf[2 * COMMAND_FRAME_SIZE];
  encodeCommand(Command{2, 5}, 10, buf);
  encodeCommand(Command{3, 6}, 11, buf + COMMAND_FRAME_SIZE);
  buf[5] ^= 0x01;  // one payload byte
  Decoder d;
  Frame f[3];
  TEST_ASSERT_EQUAL(1, decodeStream(d, buf, sizeof buf, f, 3));
  TEST_ASSERT_EQUAL_UINT32(1, d.discardedFrames());
  TEST_ASSERT_EQUAL_UINT8(11, f[0].seq);
  TEST_ASSERT_EQUAL_UINT8(3, f[0].command.motion);
}

// Scenario: Garbage between frames.
void test_garbage_between_frames() {
  uint8_t buf[64];
  int n = encodeCommand(Command{1, 1}, 1, buf);
  const uint8_t garbage[] = {0x00, 0xA5, 0x13, 0xFF, 0xA5, 0x01, 0x07, 0x42, 0x99};
  memcpy(buf + n, garbage, sizeof garbage);
  n += sizeof garbage;
  n += encodeTelemetry(sampleTelemetry(), 2, buf + n);
  Decoder d;
  Frame f[3];
  TEST_ASSERT_EQUAL(2, decodeStream(d, buf, n, f, 3));
  TEST_ASSERT_EQUAL(FrameType::Command, f[0].type);
  TEST_ASSERT_EQUAL(FrameType::Telemetry, f[1].type);
  TEST_ASSERT_EQUAL_UINT8(2, f[1].seq);
}

// Scenario: False start marker.
void test_false_start_marker() {
  uint8_t buf[1 + COMMAND_FRAME_SIZE];
  buf[0] = 0xA5;
  encodeCommand(Command{9, 3}, 7, buf + 1);
  Decoder d;
  Frame f[2];
  TEST_ASSERT_EQUAL(1, decodeStream(d, buf, sizeof buf, f, 2));
  TEST_ASSERT_EQUAL_UINT8(7, f[0].seq);
  TEST_ASSERT_EQUAL_UINT8(9, f[0].command.motion);
}

// Re-encodes a frame with a changed header byte and a recomputed CRC.
int withHeader(int offset, uint8_t value, uint8_t* buf) {
  const int n = encodeCommand(Command{2, 5}, 3, buf);
  buf[offset] = value;
  const uint16_t crc = crc16(buf + 1, n - 3);
  buf[n - 2] = static_cast<uint8_t>(crc & 0xFF);
  buf[n - 1] = static_cast<uint8_t>(crc >> 8);
  return n;
}

// Scenario: Unknown version.
void test_unknown_version_rejected_and_counted() {
  uint8_t buf[MAX_FRAME_SIZE];
  const int n = withHeader(1, 2, buf);
  Decoder d;
  Frame f[1];
  TEST_ASSERT_EQUAL(0, decodeStream(d, buf, n, f, 1));
  TEST_ASSERT_EQUAL_UINT32(1, d.discardedFrames());
}

void test_unknown_type_rejected() {
  uint8_t buf[MAX_FRAME_SIZE];
  const int n = withHeader(2, 3, buf);
  Decoder d;
  Frame f[1];
  TEST_ASSERT_EQUAL(0, decodeStream(d, buf, n, f, 1));
  TEST_ASSERT_EQUAL_UINT32(1, d.discardedFrames());
}

void test_wrong_length_rejected() {
  // A command frame header with payload length 3 and a 3-byte payload.
  uint8_t buf[16] = {0xA5, 1, 1, 5, 3, 2, 5, 0};
  const uint16_t crc = crc16(buf + 1, 4 + 3);
  buf[8] = static_cast<uint8_t>(crc & 0xFF);
  buf[9] = static_cast<uint8_t>(crc >> 8);
  Decoder d;
  Frame f[1];
  TEST_ASSERT_EQUAL(0, decodeStream(d, buf, 10, f, 1));
  TEST_ASSERT_EQUAL_UINT32(1, d.discardedFrames());
}

void test_idle_rejects_incomplete_candidate_then_returns_frame() {
  // A telemetry header (needs 33 bytes) followed by a complete command frame:
  // the candidate stays incomplete until the input ends.
  uint8_t buf[5 + COMMAND_FRAME_SIZE] = {0xA5, 1, 2, 0, 26};
  encodeCommand(Command{4, 8}, 12, buf + 5);
  Decoder d;
  Frame f;
  for (unsigned i = 0; i < sizeof buf; i++) {
    d.push(buf[i]);
    TEST_ASSERT_FALSE(d.next(f));
  }
  TEST_ASSERT_EQUAL(static_cast<int>(sizeof buf), d.pending());
  d.idle();
  TEST_ASSERT_EQUAL_UINT32(1, d.discardedFrames());
  TEST_ASSERT_TRUE(d.next(f));
  TEST_ASSERT_EQUAL_UINT8(12, f.seq);
  TEST_ASSERT_EQUAL_UINT8(4, f.command.motion);
  TEST_ASSERT_EQUAL(0, d.pending());
}

void test_pending_never_exceeds_max_frame_size() {
  // Start markers and valid-looking headers that never complete.
  Decoder d;
  Frame f;
  for (int i = 0; i < 1000; i++) {
    const uint8_t pattern[] = {0xA5, 1, 2, 0, 26};
    d.push(pattern[i % 5]);
    while (d.next(f)) {
    }
    TEST_ASSERT_TRUE(d.pending() <= 33);
  }
  // Pushing without polling at all.
  Decoder e;
  for (int i = 0; i < 1000; i++) {
    e.push(i % 7 == 0 ? 0xA5 : static_cast<uint8_t>(i));
    TEST_ASSERT_TRUE(e.pending() <= 33);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc16_check_value);
  RUN_TEST(test_sizes);
  RUN_TEST(test_command_round_trip_seq_0_and_255);
  RUN_TEST(test_telemetry_round_trip_seq_0_and_255);
  RUN_TEST(test_corrupted_frame_then_valid);
  RUN_TEST(test_garbage_between_frames);
  RUN_TEST(test_false_start_marker);
  RUN_TEST(test_unknown_version_rejected_and_counted);
  RUN_TEST(test_unknown_type_rejected);
  RUN_TEST(test_wrong_length_rejected);
  RUN_TEST(test_idle_rejects_incomplete_candidate_then_returns_frame);
  RUN_TEST(test_pending_never_exceeds_max_frame_size);
  return UNITY_END();
}
