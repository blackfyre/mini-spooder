// Drive link fuzz targets (drive-link, design D8: Decoder and datagram fuzz
// tests). Each runs one input and abort()s on a property violation. They have
// distinct names, so both link into one executable: the random driver
// (driver.cpp), the libFuzzer entry points (libfuzzer_*.cpp) and the corpus test
// (core/test/test_drive_link_fuzz_corpus). Never compiled into the firmware.
#pragma once

#include <stddef.h>
#include <stdint.h>

// What a decoder input gave, for per-seed checks.
struct FuzzDecoderSummary {
  int frames;
  uint32_t discardedBytes;
  uint32_t discardedFrames;
};

// The input as a byte stream, pushed one byte at a time with the decoder polled
// after each byte, and the end of input signalled at the end.
FuzzDecoderSummary fuzzDecoderRun(const uint8_t* data, size_t size);
void fuzzDecoderOne(const uint8_t* data, size_t size);

// The input as datagram records: [flags] [time step 0-255 ms] [length 0-40]
// [bytes]. flags bits 0-1 pick one of four senders (two addresses, two ports
// each); bit 7 runs a loop pass (the final judge and the telemetry step) after
// the datagram. A short last record takes the bytes that remain. Records start
// 4096 ms before the millis() wrap.
constexpr int kFuzzRecordMaxLength = 40;
void fuzzSessionOne(const uint8_t* data, size_t size);
