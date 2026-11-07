// The stand-alone random driver of the drive link fuzz targets (design D8),
// built and run by `mise run fuzz-link` with the host compiler. Per target it
// mutates and generates inputs from the target's corpus for the given time,
// prints its seed, and on a property violation (abort) or a crash writes the
// input to the target's corpus directory and exits non-zero.
//
//   driver --corpus <dir> --seconds <n> [--seed <n>] [--target decoder|session]
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <dirent.h>

#include <string>
#include <vector>

#include "drive_link/protocol.h"
#include "targets.h"

extern "C" const char* __asan_default_options() { return "abort_on_error=1"; }
extern "C" const char* __ubsan_default_options() { return "abort_on_error=1:print_stacktrace=1"; }

namespace {

using Bytes = std::vector<uint8_t>;

constexpr size_t kMaxInput = 600;

// The input being run, for the signal handler.
uint8_t gInput[kMaxInput];
size_t gInputSize = 0;
char gCrashPath[1024];

void onFatal(int sig) {
  const int fd = open(gCrashPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) {
    ssize_t ignored = write(fd, gInput, gInputSize);
    (void)ignored;
    close(fd);
  }
  const char msg[] = "fuzz-link: property violation or crash; input saved to ";
  ssize_t ignored = write(2, msg, sizeof msg - 1);
  ignored = write(2, gCrashPath, strlen(gCrashPath));
  ignored = write(2, "\n", 1);
  (void)ignored;
  (void)sig;
  _exit(1);
}

uint64_t gRng = 1;
uint32_t next32() {
  // xorshift64*
  gRng ^= gRng >> 12;
  gRng ^= gRng << 25;
  gRng ^= gRng >> 27;
  return static_cast<uint32_t>((gRng * 2685821657736338717ULL) >> 32);
}
uint32_t below(uint32_t n) { return n == 0 ? 0 : next32() % n; }

std::vector<Bytes> loadCorpus(const std::string& dir) {
  std::vector<Bytes> out;
  DIR* d = opendir(dir.c_str());
  if (!d) return out;
  while (dirent* e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    FILE* f = fopen((dir + "/" + e->d_name).c_str(), "rb");
    if (!f) continue;
    Bytes b;
    int c;
    while ((c = fgetc(f)) != EOF && b.size() < kMaxInput) b.push_back(static_cast<uint8_t>(c));
    fclose(f);
    out.push_back(b);
  }
  closedir(d);
  return out;
}

void mutate(Bytes& b, const std::vector<Bytes>& pool) {
  const int rounds = 1 + static_cast<int>(below(4));
  for (int i = 0; i < rounds; i++) {
    switch (below(7)) {
      case 0:  // flip a bit
        if (!b.empty()) b[below(b.size())] ^= static_cast<uint8_t>(1u << below(8));
        break;
      case 1:  // set a byte, often to an interesting value
        if (!b.empty()) {
          static const uint8_t kInteresting[] = {0x00, 0x01, 0x02, 0x03, 0x1A, 0x7F, 0x80, 0xA5, 0xFE, 0xFF};
          b[below(b.size())] = below(2) ? kInteresting[below(sizeof kInteresting)] : static_cast<uint8_t>(next32());
        }
        break;
      case 2:  // insert bytes
        if (b.size() < kMaxInput) {
          const size_t at = below(b.size() + 1);
          const size_t n = 1 + below(8);
          for (size_t k = 0; k < n && b.size() < kMaxInput; k++)
            b.insert(b.begin() + at, static_cast<uint8_t>(below(4) == 0 ? 0xA5 : next32()));
        }
        break;
      case 3:  // erase bytes
        if (!b.empty()) {
          const size_t at = below(b.size());
          const size_t n = 1 + below(8);
          b.erase(b.begin() + at, b.begin() + (at + n > b.size() ? b.size() : at + n));
        }
        break;
      case 4:  // splice from another input
        if (!pool.empty()) {
          const Bytes& o = pool[below(pool.size())];
          if (!o.empty()) {
            const size_t from = below(o.size());
            const size_t n = 1 + below(o.size() - from);
            const size_t at = below(b.size() + 1);
            b.insert(b.begin() + at, o.begin() + from, o.begin() + from + n);
          }
        }
        break;
      case 5:  // duplicate a chunk
        if (!b.empty() && b.size() < kMaxInput) {
          const size_t from = below(b.size());
          const size_t n = 1 + below(b.size() - from);
          Bytes chunk(b.begin() + from, b.begin() + from + n);
          b.insert(b.begin() + below(b.size() + 1), chunk.begin(), chunk.end());
        }
        break;
      case 6:  // append an encoded frame
      {
        uint8_t f[ms::drive_link::MAX_FRAME_SIZE];
        int n;
        if (below(2)) {
          n = ms::drive_link::encodeCommand(
              ms::drive_link::Command{static_cast<uint8_t>(below(4) ? below(16) : next32()),
                                      static_cast<uint8_t>(next32())},
              static_cast<uint8_t>(next32()), f);
        } else {
          ms::drive_link::Telemetry t;
          uint8_t* raw = reinterpret_cast<uint8_t*>(&t);
          for (size_t k = 0; k < sizeof t; k++) raw[k] = static_cast<uint8_t>(next32());
          n = ms::drive_link::encodeTelemetry(t, static_cast<uint8_t>(next32()), f);
        }
        b.insert(b.end(), f, f + n);
        break;
      }
    }
  }
  if (b.size() > kMaxInput) b.resize(kMaxInput);
}

// A structured session input: mostly valid command frames from a few senders,
// with sequence numbers near the last one, changing counters, gaps, floods and
// corruption, so runs reach sessions, requests, jumps and lapses.
Bytes generateSession() {
  Bytes b;
  const int records = 1 + static_cast<int>(below(40));
  uint8_t seq[4] = {static_cast<uint8_t>(next32()), static_cast<uint8_t>(next32()), 0, 0};
  uint8_t counter[4] = {static_cast<uint8_t>(next32()), 0, 0, 0};
  for (int r = 0; r < records && b.size() + 3 + kFuzzRecordMaxLength <= kMaxInput; r++) {
    const uint8_t sender = static_cast<uint8_t>(below(5) == 0 ? below(4) : 0);
    uint8_t flags = sender;
    if (below(2)) flags |= 0x80;
    uint8_t step;
    switch (below(6)) {
      case 0: step = 0; break;
      case 1: step = static_cast<uint8_t>(below(20)); break;
      case 2: step = static_cast<uint8_t>(250 + below(6)); break;
      case 3: step = static_cast<uint8_t>(below(256)); break;
      default: step = 50; break;
    }
    uint8_t f[ms::drive_link::MAX_FRAME_SIZE + 8];
    int n;
    switch (below(10)) {
      case 0: {  // random bytes
        n = static_cast<int>(below(kFuzzRecordMaxLength + 1));
        for (int k = 0; k < n; k++) f[k] = static_cast<uint8_t>(next32());
        break;
      }
      case 1: {  // a corrupted command frame
        n = ms::drive_link::encodeCommand(ms::drive_link::Command{2, counter[sender]}, seq[sender], f);
        f[below(n)] ^= static_cast<uint8_t>(1u << below(8));
        break;
      }
      default: {  // a valid command frame
        const uint32_t pick = below(10);
        if (pick < 7)
          seq[sender] = static_cast<uint8_t>(seq[sender] + 1);
        else if (pick == 7)
          seq[sender] = static_cast<uint8_t>(seq[sender] + below(256));
        else if (pick == 8)
          seq[sender] = static_cast<uint8_t>(seq[sender] - below(3));
        if (below(4) == 0) counter[sender] = static_cast<uint8_t>(counter[sender] + 1 + below(3));
        const uint8_t motion = static_cast<uint8_t>(below(5) == 0 ? next32() : below(16));
        n = ms::drive_link::encodeCommand(ms::drive_link::Command{motion, counter[sender]}, seq[sender], f);
        if (below(20) == 0) f[n++] = static_cast<uint8_t>(next32());  // one byte too many
        break;
      }
    }
    b.push_back(flags);
    b.push_back(step);
    b.push_back(static_cast<uint8_t>(n));
    b.insert(b.end(), f, f + n);
  }
  return b;
}

int runTarget(const char* name, void (*target)(const uint8_t*, size_t), const std::string& corpusRoot,
              double seconds, uint64_t seed) {
  const std::string dir = corpusRoot + "/" + name;
  std::vector<Bytes> corpus = loadCorpus(dir);
  if (corpus.empty()) fprintf(stderr, "fuzz-link: warning: no seeds in %s\n", dir.c_str());
  snprintf(gCrashPath, sizeof gCrashPath, "%s/crash-%llu", dir.c_str(), static_cast<unsigned long long>(seed));
  std::vector<Bytes> pool = corpus;
  const bool session = strcmp(name, "session") == 0;

  auto run = [&](const Bytes& in) {
    gInputSize = in.size() > kMaxInput ? kMaxInput : in.size();
    memcpy(gInput, in.data(), gInputSize);
    target(gInput, gInputSize);
  };

  for (const Bytes& in : corpus) run(in);

  timespec start;
  clock_gettime(CLOCK_MONOTONIC, &start);
  uint64_t runs = 0;
  for (;;) {
    if ((runs & 255) == 0) {
      timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      const double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;
      if (elapsed >= seconds) break;
    }
    Bytes in;
    const uint32_t how = below(10);
    if (session && how < 4) {
      in = generateSession();
    } else if (how == 4 || pool.empty()) {
      in.resize(below(kMaxInput / 4));
      for (uint8_t& x : in) x = static_cast<uint8_t>(next32());
    } else {
      in = pool[below(pool.size())];
      mutate(in, pool);
    }
    run(in);
    ++runs;
    if (pool.size() < 512)
      pool.push_back(in);
    else if (below(4) == 0)
      pool[corpus.size() + below(pool.size() - corpus.size())] = in;
  }
  printf("fuzz-link: %s: %llu inputs in %.0f s, %zu seeds, no violation\n", name,
         static_cast<unsigned long long>(runs), seconds, corpus.size());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string corpus;
  double seconds = 60;
  uint64_t seed = 0;
  bool seedGiven = false;
  std::string only;
  for (int i = 1; i + 1 < argc; i += 2) {
    if (strcmp(argv[i], "--corpus") == 0)
      corpus = argv[i + 1];
    else if (strcmp(argv[i], "--seconds") == 0)
      seconds = atof(argv[i + 1]);
    else if (strcmp(argv[i], "--seed") == 0)
      seed = strtoull(argv[i + 1], nullptr, 10), seedGiven = true;
    else if (strcmp(argv[i], "--target") == 0)
      only = argv[i + 1];
    else {
      fprintf(stderr, "usage: %s --corpus <dir> --seconds <n> [--seed <n>] [--target decoder|session]\n", argv[0]);
      return 2;
    }
  }
  if (corpus.empty() || seconds <= 0) {
    fprintf(stderr, "usage: %s --corpus <dir> --seconds <n> [--seed <n>] [--target decoder|session]\n", argv[0]);
    return 2;
  }
  if (!seedGiven) {
    timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    seed = static_cast<uint64_t>(t.tv_sec) * 1000003u ^ static_cast<uint64_t>(t.tv_nsec) ^ static_cast<uint64_t>(getpid());
  }
  gRng = seed ? seed : 1;
  printf("fuzz-link: seed %llu\n", static_cast<unsigned long long>(seed));
  fflush(stdout);

  signal(SIGABRT, onFatal);
  signal(SIGSEGV, onFatal);
  signal(SIGBUS, onFatal);
  signal(SIGILL, onFatal);
  signal(SIGFPE, onFatal);

  if (only.empty() || only == "decoder") runTarget("decoder", fuzzDecoderOne, corpus, seconds, seed);
  if (only.empty() || only == "session") runTarget("session", fuzzSessionOne, corpus, seconds, seed);
  return 0;
}
