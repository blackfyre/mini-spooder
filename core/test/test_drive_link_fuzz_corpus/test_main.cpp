// The drive link fuzz corpus as deterministic tests (drive-link: Bounded fuzz
// runs, Deterministic gate): every file in core/fuzz/drive_link/corpus/<target>/
// (golden seeds and saved failing inputs) runs once through its target, which
// aborts on a property violation. Golden seeds also check their own outcome
// (Golden seeds for the decoder, Golden seeds for datagram acceptance). Only this
// test compiles the fuzz targets; the library and the firmware never do.
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "../../fuzz/drive_link/targets.cpp"

void setUp() {}
void tearDown() {}

namespace {

void corpusDir(const char* target, char* out, int size) {
  const char* file = __FILE__;
  const char* cut = strstr(file, "test/test_drive_link_fuzz_corpus/");
  const int prefix = cut ? static_cast<int>(cut - file) : 0;
  snprintf(out, size, "%.*sfuzz/drive_link/corpus/%s", prefix, file, target);
}

int readFile(const char* path, uint8_t* buf, int size) {
  FILE* f = fopen(path, "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
  const int n = static_cast<int>(fread(buf, 1, size, f));
  fclose(f);
  return n;
}

bool startsWith(const char* s, const char* prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

// Runs every file of a target's corpus; returns the number of golden seeds.
int runCorpus(const char* target, void (*check)(const char* name, const uint8_t* data, int n)) {
  char dir[1024];
  corpusDir(target, dir, sizeof dir);
  DIR* d = opendir(dir);
  TEST_ASSERT_NOT_NULL_MESSAGE(d, dir);
  int golden = 0;
  int files = 0;
  while (dirent* e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    char path[2048];
    snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
    static uint8_t buf[4096];
    const int n = readFile(path, buf, sizeof buf);
    check(e->d_name, buf, n);
    ++files;
    if (startsWith(e->d_name, "golden-")) ++golden;
  }
  closedir(d);
  TEST_ASSERT_TRUE_MESSAGE(files > 0, dir);
  return golden;
}

void checkDecoderSeed(const char* name, const uint8_t* data, int n) {
  const FuzzDecoderSummary s = fuzzDecoderRun(data, static_cast<size_t>(n));
  if (!startsWith(name, "golden-")) return;
  if (startsWith(name, "golden-reject_")) {
    TEST_ASSERT_EQUAL_MESSAGE(0, s.frames, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(n), s.discardedBytes, name);
    TEST_ASSERT_TRUE_MESSAGE(s.discardedFrames >= 1, name);
  } else {
    TEST_ASSERT_EQUAL_MESSAGE(1, s.frames, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s.discardedBytes, name);
  }
}

void checkSessionSeed(const char* name, const uint8_t* data, int n) {
  fuzzSessionOne(data, static_cast<size_t>(n));
  if (!startsWith(name, "golden-")) return;
  // The seed is one record holding the golden frame intact.
  TEST_ASSERT_TRUE_MESSAGE(n >= 3 && data[2] == n - 3, name);
  CheckingPorts ports;
  Link link(ports);
  ports.link = &link;
  const AcceptResult r = link.receive(data + 3, n - 3, Sender{1, 2}, 1000);
  if (startsWith(name, "golden-command_")) {
    TEST_ASSERT_EQUAL_MESSAGE(Outcome::Accepted, r.outcome, name);
    TEST_ASSERT_TRUE_MESSAGE(r.sessionStarted, name);
    TEST_ASSERT_EQUAL_MESSAGE(RequestKind::None, r.request.kind, name);
  } else {
    TEST_ASSERT_EQUAL_MESSAGE(Outcome::Malformed, r.outcome, name);
    TEST_ASSERT_FALSE_MESSAGE(link.live(), name);
  }
}

int goldenLines() {
  char path[1024];
  const char* file = __FILE__;
  const char* cut = strstr(file, "test_drive_link_fuzz_corpus/");
  const int prefix = cut ? static_cast<int>(cut - file) : 0;
  snprintf(path, sizeof path, "%.*sgolden/drive_link_v1.txt", prefix, file);
  FILE* f = fopen(path, "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
  char line[1024];
  int n = 0;
  while (fgets(line, sizeof line, f))
    if (line[0] != '#' && line[0] != '\n') ++n;
  fclose(f);
  return n;
}

}  // namespace

void test_decoder_corpus() { TEST_ASSERT_EQUAL(goldenLines(), runCorpus("decoder", checkDecoderSeed)); }

void test_session_corpus() { TEST_ASSERT_EQUAL(goldenLines(), runCorpus("session", checkSessionSeed)); }

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_decoder_corpus);
  RUN_TEST(test_session_corpus);
  return UNITY_END();
}
