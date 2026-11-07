// Diagnostic output on UART0 at 115200 baud. Lines are queued and written as the
// UART has room, so that no loop pass waits for the serial port.
#pragma once

#include <stddef.h>

namespace diag {

// Starts UART0. Called once at the start of setup().
void begin();

// Queues one line (CR LF added). Lines over the queue's room are cut short.
void line(const char* text);

// Writes as much queued output as the UART takes without waiting. Called every pass.
void poll();

// Waits until every queued byte is written. Only for setup() and a restart.
void flush();

}  // namespace diag
