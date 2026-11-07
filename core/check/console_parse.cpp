#include "check/console_parse.h"

#include <string.h>

namespace ms {
namespace check {
namespace {

constexpr int kMaxWords = 10;
constexpr int kNumberLimit = 999;

struct Word {
  const char* p;
  size_t n;
};

bool equals(const Word& w, const char* s) {
  const size_t n = strlen(s);
  return w.n == n && memcmp(w.p, s, n) == 0;
}

bool toInt(const Word& w, int& out) {
  size_t i = 0;
  bool negative = false;
  if (w.n > 0 && (w.p[0] == '-' || w.p[0] == '+')) {
    negative = w.p[0] == '-';
    i = 1;
  }
  if (i == w.n) return false;
  int v = 0;
  for (; i < w.n; ++i) {
    const char c = w.p[i];
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
    if (v > kNumberLimit) return false;
  }
  out = negative ? -v : v;
  return true;
}

bool toServo(const Word& w, int& out) { return toInt(w, out) && isServoIndex(out); }

bool toPose(const Word* words, Pose& pose) {
  for (int i = 0; i < kServoCount; ++i) {
    int v;
    if (!toInt(words[i], v)) return false;
    pose.angle[i] = static_cast<int16_t>(v);
  }
  return true;
}

}  // namespace

bool parseLine(const char* line, size_t len, Command& out) {
  Word words[kMaxWords];
  int count = 0;
  size_t i = 0;
  while (i < len) {
    const char c = line[i];
    if (c == ' ' || c == '\t' || c == '\r') {
      ++i;
      continue;
    }
    if (c < 0x21 || c > 0x7e) return false;
    const size_t start = i;
    while (i < len && line[i] > 0x20 && line[i] < 0x7f) ++i;
    if (count == kMaxWords) return false;
    words[count++] = Word{line + start, i - start};
  }
  if (count == 0) return false;

  const Word& verb = words[0];
  const int args = count - 1;
  out = Command{};
  if (equals(verb, "enable") || equals(verb, "pose")) {
    out.kind = equals(verb, "enable") ? CommandKind::Enable : CommandKind::Pose;
    return args == kServoCount && toPose(words + 1, out.pose);
  }
  if (equals(verb, "servo")) {
    out.kind = CommandKind::Servo;
    return args == 2 && toServo(words[1], out.servo) && toInt(words[2], out.value);
  }
  if (equals(verb, "release")) {
    if (args != 1) return false;
    if (equals(words[1], "all")) {
      out.kind = CommandKind::ReleaseAll;
      return true;
    }
    out.kind = CommandKind::Release;
    return toServo(words[1], out.servo);
  }
  if (equals(verb, "trim")) {
    out.kind = CommandKind::Trim;
    return args == 2 && toServo(words[1], out.servo) && toInt(words[2], out.value);
  }
  struct Bare {
    const char* name;
    CommandKind kind;
  };
  static const Bare kBare[] = {
      {"safe", CommandKind::Safe},       {"trimreset", CommandKind::TrimReset},
      {"status", CommandKind::Status},   {"restart", CommandKind::Restart},
      {"hang", CommandKind::Hang},
  };
  for (const Bare& b : kBare) {
    if (equals(verb, b.name)) {
      out.kind = b.kind;
      return args == 0;
    }
  }
  return false;
}

}  // namespace check
}  // namespace ms
