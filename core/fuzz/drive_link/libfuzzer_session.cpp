// libFuzzer entry point for the datagram acceptance target, for a later
// `clang++ -fsanitize=fuzzer` build (one executable per target).
#include <stddef.h>
#include <stdint.h>

#include "targets.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fuzzSessionOne(data, size);
  return 0;
}
