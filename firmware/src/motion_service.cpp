#include "motion_service.h"

#include <Arduino.h>

#include "motion/gate_output.h"

#ifdef MOTION_DEMO
#include "diag.h"
#include "motion/demo_script.h"
#endif

namespace motion_service {
namespace {

ms::MotionPlayer* player = nullptr;
ms::MotionBinding* linkBinding = nullptr;
ms::MotionBinding* portalBinding = nullptr;

#ifdef MOTION_DEMO
void demoLog(void*, const char* line) { diag::line(line); }

ms::MotionDemo* demo = nullptr;
char demoLine[8];
size_t demoLen = 0;

// Reads the serial console without waiting; a line `go` starts the sequence.
void pollConsole(uint32_t nowMs) {
  int avail = Serial.available();
  while (avail-- > 0) {
    const int c = Serial.read();
    if (c < 0) return;
    if (c == '\r' || c == '\n') {
      if (demoLen == 2 && demoLine[0] == 'g' && demoLine[1] == 'o') demo->go(nowMs);
      demoLen = 0;
    } else if (demoLen < sizeof demoLine) {
      demoLine[demoLen++] = static_cast<char>(c);  // an overlong line is never `go`
    }
  }
}
#endif

}  // namespace

void begin(ms::OutputGate& gate) {
  static ms::GateMotionOutput gateOutput(gate);
  static ms::MotionPlayer motionPlayer(gateOutput);
  static ms::MotionBinding linkB(motionPlayer, ms::Origin::Link);
  static ms::MotionBinding portalB(motionPlayer, ms::Origin::Portal);
  player = &motionPlayer;
  linkBinding = &linkB;
  portalBinding = &portalB;
#ifdef MOTION_DEMO
  static ms::MotionDemo motionDemo(motionPlayer, demoLog, nullptr);
  demo = &motionDemo;
  diag::line("motion-demo: send go");
#endif
}

void poll(uint32_t nowMs) {
#ifdef MOTION_DEMO
  pollConsole(nowMs);
  const uint32_t startUs = micros();
  player->service(nowMs);
  demo->noteServiceUs(micros() - startUs);
  demo->poll(nowMs);
#else
  player->service(nowMs);
#endif
}

ms::MotionBinding& link() { return *linkBinding; }
ms::MotionBinding& portal() { return *portalBinding; }
ms::PlayerState state() { return player->state(); }

}  // namespace motion_service
