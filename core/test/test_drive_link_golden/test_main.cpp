// Golden frames of drive link protocol version 1 (drive-link: Golden frames): a
// fixed set of frames encoded by the C++ encoder and compared with
// core/test/golden/drive_link_v1.txt, which the pak's Go codec tests against.
// After a deliberate protocol change (which also changes the version), regenerate
// the file with `MINI_SPOODER_WRITE_GOLDEN=1 mise run test` and review the diff.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "drive_link/protocol.h"

using namespace ms::drive_link;

void setUp() {}
void tearDown() {}

namespace {

const char* const kHeader =
    "# Drive link protocol version 1: golden frames.\n"
    "# Generated from the C++ encoder by core/test/test_drive_link_golden; regenerate\n"
    "# with MINI_SPOODER_WRITE_GOLDEN=1 mise run test after a deliberate protocol\n"
    "# change, which also changes the protocol version.\n"
    "#\n"
    "# One frame per line, fields separated by single spaces:\n"
    "#   <name> <frame bytes as lowercase hex, no separators> <field>=<value> ...\n"
    "# Values are decimal. The header fields (version, type, seq) come first, then\n"
    "# the payload fields in payload order. A line whose only field is \"rejected\"\n"
    "# is a frame a decoder must discard. Lines starting with # are comments.\n";

struct Text {
  char buf[16384];
  int len;
};

void add(Text& t, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void add(Text& t, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  t.len += vsnprintf(t.buf + t.len, sizeof(t.buf) - t.len, fmt, ap);
  va_end(ap);
  TEST_ASSERT_TRUE_MESSAGE(t.len < static_cast<int>(sizeof(t.buf)), "golden text buffer too small");
}

void describe(Text& t, const Frame& f) {
  add(t, "version=%d type=%s seq=%u", VERSION, f.type == FrameType::Command ? "command" : "telemetry", f.seq);
  if (f.type == FrameType::Command) {
    add(t, " motion=%u counter=%u", f.command.motion, f.command.counter);
    return;
  }
  const Telemetry& m = f.telemetry;
  add(t, " playing=%u pending=%u counter=%u result=%u commandSeq=%u ageMs=%u", m.playing, m.pending, m.counter,
      m.result, m.commandSeq, m.ageMs);
  for (int i = 0; i < JOINT_COUNT; i++) add(t, " angle%d=%u", i, m.angles[i]);
  add(t, " accepted=%u malformed=%u outOfSequence=%u otherSender=%u rateLimited=%u failsafes=%u", m.accepted,
      m.malformed, m.outOfSequence, m.otherSender, m.rateLimited, m.failsafes);
}

int encode(const Frame& f, uint8_t* out) {
  return f.type == FrameType::Command ? encodeCommand(f.command, f.seq, out) : encodeTelemetry(f.telemetry, f.seq, out);
}

void addHex(Text& t, const uint8_t* bytes, int n) {
  for (int i = 0; i < n; i++) add(t, "%02x", bytes[i]);
}

// Decodes bytes with a fresh decoder; returns the number of frames.
int decodeAll(const uint8_t* bytes, int n, Frame& last, Decoder& d) {
  int frames = 0;
  Frame f;
  for (int i = 0; i < n; i++) {
    d.push(bytes[i]);
    while (d.next(f)) {
      last = f;
      frames++;
    }
  }
  while (d.flush(f)) {
    last = f;
    frames++;
  }
  return frames;
}

void addFrame(Text& t, const char* name, const Frame& f) {
  uint8_t bytes[MAX_FRAME_SIZE];
  const int n = encode(f, bytes);
  Frame decoded;
  Decoder d;
  TEST_ASSERT_EQUAL_MESSAGE(1, decodeAll(bytes, n, decoded, d), name);
  Text expected = {}, actual = {};
  describe(expected, f);
  describe(actual, decoded);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(expected.buf, actual.buf, name);
  add(t, "%s ", name);
  addHex(t, bytes, n);
  add(t, " %s\n", actual.buf);
}

void addRejected(Text& t, const char* name, const uint8_t* bytes, int n) {
  Frame f;
  Decoder d;
  TEST_ASSERT_EQUAL_MESSAGE(0, decodeAll(bytes, n, f, d), name);
  TEST_ASSERT_TRUE_MESSAGE(d.discardedFrames() >= 1, name);
  add(t, "%s ", name);
  addHex(t, bytes, n);
  add(t, " rejected\n");
}

Frame command(uint8_t seq, uint8_t motion, uint8_t counter) {
  Frame f = {};
  f.type = FrameType::Command;
  f.seq = seq;
  f.command = Command{motion, counter};
  return f;
}

Frame telemetry(uint8_t seq, uint8_t playing, uint8_t pending, uint8_t counter, Result result, uint8_t commandSeq,
                uint8_t ageMs, const uint8_t (&angles)[JOINT_COUNT], uint16_t a, uint16_t m, uint16_t o, uint16_t s,
                uint16_t r, uint16_t fs) {
  Frame f = {};
  f.type = FrameType::Telemetry;
  f.seq = seq;
  Telemetry& t = f.telemetry;
  t.playing = playing;
  t.pending = pending;
  t.counter = counter;
  t.result = static_cast<uint8_t>(result);
  t.commandSeq = commandSeq;
  t.ageMs = ageMs;
  memcpy(t.angles, angles, sizeof angles);
  t.accepted = a;
  t.malformed = m;
  t.outOfSequence = o;
  t.otherSender = s;
  t.rateLimited = r;
  t.failsafes = fs;
  return f;
}

// Sets the CRC of a frame of `n` bytes after its header or payload was edited.
void fixCrc(uint8_t* bytes, int n) {
  const uint16_t crc = crc16(bytes + 1, n - 3);
  bytes[n - 2] = static_cast<uint8_t>(crc & 0xFF);
  bytes[n - 1] = static_cast<uint8_t>(crc >> 8);
}

void buildGolden(Text& t) {
  add(t, "%s", kHeader);

  const uint8_t released[JOINT_COUNT] = {255, 255, 255, 255, 255, 255, 255, 255};
  const uint8_t standby[JOINT_COUNT] = {70, 90, 90, 110, 110, 90, 90, 70};
  const uint8_t zero[JOINT_COUNT] = {135, 45, 135, 45, 45, 135, 45, 135};
  const uint8_t mixed[JOINT_COUNT] = {45, 135, 90, 0, 180, 1, 255, 3};
  const uint8_t lows[JOINT_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};

  addFrame(t, "command_idle_zero", command(0, 0, 0));
  addFrame(t, "command_forward", command(17, 2, 5));
  addFrame(t, "command_stop", command(42, 0, 6));
  addFrame(t, "command_centre", command(4, 99, 10));
  addFrame(t, "command_zero_position", command(3, 100, 9));
  addFrame(t, "command_standby_seq_wrap", command(255, 1, 0));
  addFrame(t, "command_extremes_seq_wrap", command(255, 255, 255));

  addFrame(t, "telemetry_baseline_none",
           telemetry(0, 0, 0, 0, Result::None, 0, 255, released, 0, 0, 0, 0, 0, 0));
  addFrame(t, "telemetry_playing_accepted",
           telemetry(7, 2, 0, 5, Result::Accepted, 40, 10, standby, 1000, 3, 2, 1, 0, 1));
  addFrame(t, "telemetry_pending_accepted",
           telemetry(8, 11, 9, 7, Result::Accepted, 41, 0, mixed, 258, 0, 0, 0, 0, 0));
  addFrame(t, "telemetry_stop_requested",
           telemetry(9, 0, 0, 8, Result::StopRequested, 42, 254, standby, 1, 2, 3, 4, 5, 6));
  addFrame(t, "telemetry_rejected", telemetry(10, 0, 0, 9, Result::Rejected, 43, 12, zero, 0, 0, 0, 0, 0, 0));
  addFrame(t, "telemetry_age_255", telemetry(11, 1, 0, 3, Result::Accepted, 200, 255, standby, 17, 0, 0, 0, 0, 0));
  addFrame(t, "telemetry_centre_playing",
           telemetry(128, 99, 100, 1, Result::Accepted, 0, 1, lows, 65534, 1, 32768, 0, 0, 0));
  addFrame(t, "telemetry_extremes_seq_wrap",
           telemetry(255, 255, 255, 255, Result::Rejected, 255, 255, released, 65535, 65535, 65535, 65535, 65535,
                     65535));

  uint8_t bytes[MAX_FRAME_SIZE + 1];
  int n = encodeCommand(Command{2, 5}, 5, bytes);
  bytes[n - 1] ^= 0x01;
  addRejected(t, "reject_bad_crc_command", bytes, n);

  n = encodeTelemetry(telemetry(6, 0, 0, 0, Result::None, 0, 0, lows, 0, 0, 0, 0, 0, 0).telemetry, 6, bytes);
  bytes[n - 2] ^= 0x80;
  addRejected(t, "reject_bad_crc_telemetry", bytes, n);

  n = encodeCommand(Command{2, 5}, 5, bytes);
  bytes[1] = 2;
  fixCrc(bytes, n);
  addRejected(t, "reject_version_2_command", bytes, n);

  n = encodeCommand(Command{2, 5}, 5, bytes);
  bytes[2] = 3;
  fixCrc(bytes, n);
  addRejected(t, "reject_unknown_type_3", bytes, n);

  // A command header with payload length 3 and a 3-byte payload.
  const uint8_t length3[] = {START, VERSION, 1, 5, 3, 2, 5, 0, 0, 0};
  memcpy(bytes, length3, sizeof length3);
  fixCrc(bytes, sizeof length3);
  addRejected(t, "reject_command_length_3", bytes, sizeof length3);
}

void goldenPath(char* out, int size) {
  const char* file = __FILE__;
  const char* cut = strstr(file, "test_drive_link_golden/");
  const int prefix = cut ? static_cast<int>(cut - file) : 0;
  snprintf(out, size, "%.*sgolden/drive_link_v1.txt", prefix, file);
}

}  // namespace

void test_golden_frames() {
  static Text t;
  t = {};
  buildGolden(t);

  char path[1024];
  goldenPath(path, sizeof path);
  const char* write = getenv("MINI_SPOODER_WRITE_GOLDEN");
  if (write && write[0] == '1') {
    FILE* f = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "cannot write core/test/golden/drive_link_v1.txt");
    fwrite(t.buf, 1, t.len, f);
    fclose(f);
  }

  static char golden[sizeof(t.buf)];
  FILE* f = fopen(path, "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, "cannot read core/test/golden/drive_link_v1.txt");
  const int n = static_cast<int>(fread(golden, 1, sizeof(golden) - 1, f));
  fclose(f);
  golden[n] = 0;

  // Compare line by line, so a failure names the frame.
  const char* g = golden;
  const char* e = t.buf;
  while (*g || *e) {
    const char* gEnd = strchr(g, '\n');
    const char* eEnd = strchr(e, '\n');
    const int gLen = gEnd ? static_cast<int>(gEnd - g) : static_cast<int>(strlen(g));
    const int eLen = eEnd ? static_cast<int>(eEnd - e) : static_cast<int>(strlen(e));
    if (gLen != eLen || strncmp(g, e, gLen) != 0) {
      char message[512];
      const int nameLen = static_cast<int>(strcspn(g, " \n"));
      snprintf(message, sizeof message, "golden frame %.*s differs: golden \"%.*s\", encoder \"%.*s\"", nameLen, g,
               gLen > 200 ? 200 : gLen, g, eLen > 200 ? 200 : eLen, e);
      TEST_FAIL_MESSAGE(message);
    }
    g += gLen + (gEnd ? 1 : 0);
    e += eLen + (eEnd ? 1 : 0);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_golden_frames);
  return UNITY_END();
}
