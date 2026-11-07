#include "trace.h"

#ifdef MINI_SPOODER_TRACE

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <stdio.h>

#include "diag.h"

namespace trace {

namespace {

const ms::OutputGate* gate = nullptr;
const ms::LoopStats* stats = nullptr;
WiFiEventHandler joinHandler;
WiFiEventHandler leaveHandler;
uint32_t nextBeatMs = 0;
uint32_t lastPasses = 0;

void station(const char* what, const uint8_t mac[6], uint8_t aid) {
  char buf[64];
  snprintf(buf, sizeof buf, "trace sta %s %02x:%02x:%02x:%02x:%02x:%02x aid %u t %lu", what, mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5], aid, static_cast<unsigned long>(millis()));
  diag::line(buf);
}

}  // namespace

void begin(const ms::OutputGate& g, const ms::LoopStats& s) {
  gate = &g;
  stats = &s;
  joinHandler = WiFi.onSoftAPModeStationConnected(
      [](const WiFiEventSoftAPModeStationConnected& e) { station("join", e.mac, e.aid); });
  leaveHandler = WiFi.onSoftAPModeStationDisconnected(
      [](const WiFiEventSoftAPModeStationDisconnected& e) { station("leave", e.mac, e.aid); });
  diag::line("trace on");
}

void poll(uint32_t nowMs) {
  if (static_cast<int32_t>(nowMs - nextBeatMs) < 0) return;
  nextBeatMs = nowMs + 5000;
  char buf[160];
  snprintf(buf, sizeof buf,
           "trace beat t %lu sta %u heap %u passes/5s %lu longest_us %lu over %lu mode %s session %d",
           static_cast<unsigned long>(nowMs), WiFi.softAPgetStationNum(), ESP.getFreeHeap(),
           static_cast<unsigned long>(stats->passes() - lastPasses),
           static_cast<unsigned long>(stats->longestPassUs()), static_cast<unsigned long>(stats->overBudgetCount()),
           ms::modeName(gate->mode()), gate->sessionLive() ? 1 : 0);
  lastPasses = stats->passes();
  diag::line(buf);
}

}  // namespace trace

#else

namespace trace {
void begin(const ms::OutputGate&, const ms::LoopStats&) {}
void poll(uint32_t) {}
}  // namespace trace

#endif
