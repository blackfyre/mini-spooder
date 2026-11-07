#include "portal.h"

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <stdio.h>

#include <memory>

#include "calibration/trim_table.h"
#include "portal/request_handler.h"
#include "portal_motion.h"
#include "portal_pages.h"  // generated into the build directory (scripts/portal_pages.py)

namespace portal {

namespace {

namespace ph = ms::portal;

// Longest wait for a request line or header that has only partly arrived. The
// library reads them with the client's stream timeout (5 s for a WiFiClient),
// which would hold the main loop; complete requests are unaffected.
constexpr unsigned long kParseTimeoutMs = 20;
// Longest wait for room in the send buffer while a response is written (the
// library sets 5 s); a healthy client acknowledges well within it.
constexpr unsigned long kSendTimeoutMs = 500;

// ESP8266WebServer with the parse timeout applied before the library reads a
// request, including the first request of a newly accepted connection.
class PortalServer : public ESP8266WebServer {
 public:
  explicit PortalServer(int port) : ESP8266WebServer(port) {}

  void serve() {
    if (_currentStatus == HC_NONE) {
      // The library's own accept step (handleClient), with the timeout set first.
      WiFiClient client = _server.accept();
      if (!client) return;
      client.setTimeout(kParseTimeoutMs);
      _currentClient = client;
      _currentStatus = HC_WAIT_READ;
      _statusChange = millis();
    } else {
      _currentClient.setTimeout(kParseTimeoutMs);
    }
    handleClient();
  }
};

PortalServer server(80);
ms::OutputGate* gate = nullptr;
CalibrationStore* calibration = nullptr;

const char* contentType(ph::ContentType type) {
  return type == ph::ContentType::Html ? "text/html" : "text/plain";
}

void sendBody(const ph::Result& r) {
  String body;
  body.reserve(sizeof r.head + 16);
  body += r.head;
  if (r.echo != nullptr) body += r.echo;
  if (r.tail != nullptr) body += r.tail;
  server.send(r.status, contentType(r.type), body);
}

// The calibration page from flash, each {{OFF<i>}} replaced by servo i's trim.
// The length is known up front, so HTTP/1.0 clients get the page too.
void sendSetting() {
  const ms::TrimTable& trims = calibration->table();
  char values[ms::kServoCount][8];
  size_t total = 0;
  for (size_t i = 0; i < kPageSettingPartCount; ++i) total += kPageSettingPartLengths[i];
  for (int i = 0; i < ms::kServoCount; ++i) {
    const int n = snprintf(values[i], sizeof values[i], "%d", trims.trim(i));
    total += n > 0 ? static_cast<size_t>(n) : 0;
  }
  server.setContentLength(total);
  server.send(200, "text/html", "");
  for (size_t i = 0; i < kPageSettingPartCount; ++i) {
    server.sendContent_P(kPageSettingParts[i], kPageSettingPartLengths[i]);
    if (i < ms::kServoCount) server.sendContent(values[kPageSettingTrimAfterPart[i]]);
  }
}

void sendPage(ph::Page page) {
  switch (page) {
    case ph::Page::Controller:
      server.send_P(200, "text/html", kPageController, sizeof kPageController - 1);
      return;
    case ph::Page::Zero:
      server.send_P(200, "text/html", kPageZero, sizeof kPageZero - 1);
      return;
    case ph::Page::Editor:
      server.send_P(200, "text/html", kPageEditor, sizeof kPageEditor - 1);
      return;
    case ph::Page::Setting:
      sendSetting();
      return;
    case ph::Page::None:
      return;
  }
}

bool motionId(int32_t id) { return portal_motion::isMotionId(id); }

// The actions on the capabilities' interfaces. The portal holds no writer token
// and never calls enable: a direct servo command goes through servo-output's
// token-free direct servo command, which owns switching output on.
class Glue : public ph::Executor {
 public:
  void requestMotion(int32_t motion) override { portal_motion::request(motion); }
  void stopMotion() override { portal_motion::stop(); }
  bool directServo(int servo, int angle) override { return gate->directServo(servo, angle); }
  bool storeTrim(int servo, int value) override {
    // Index and range are checked already; a rejection is a failed flash commit.
    return calibration->setTrim(servo, value) != ms::TrimSetResult::Rejected;
  }
  bool resetTrims() override { return calibration->resetAll(); }
  bool playKeyframe(const uint8_t angles[ms::kServoCount], uint16_t durationMs) override {
    return portal_motion::playKeyframe(angles, durationMs);
  }
};

// Called by the library once the request line is read, before the headers.
// The installed ESP8266WebServer parses an unknown method (TRACE, PATCH, ...) as
// GET, and would wait up to 5 s for a declared POST body; so every request that
// is not GET is answered here with the handler's 404 and the connection closed.
bool requestIsGet = false;

ESP8266WebServer::ClientFuture screenMethod(const String& method, const String& url,
                                            WiFiClient* client,
                                            ESP8266WebServer::ContentTypeFunction) {
  requestIsGet = method == F("GET");
  if (requestIsGet) return ESP8266WebServer::CLIENT_REQUEST_CAN_CONTINUE;
  const int query = url.indexOf('?');
  const String path = query < 0 ? url : url.substring(0, query);
  const ph::Request req{ph::Method::Other, path.c_str(), nullptr, 0};
  const ph::Context ctx{gate->sessionLive(), gate->mode(), false, nullptr, ms::kTrimMin,
                        ms::kTrimMax};
  const ph::Result r = ph::handle(req, ctx);  // 404 Not found: <path>, no action
  String body;
  body += r.head;
  if (r.echo != nullptr) body += r.echo;
  if (r.tail != nullptr) body += r.tail;
  String head;
  head.reserve(96);
  head += F("HTTP/1.1 ");
  head += r.status;
  head += F(" Not Found\r\nContent-Type: text/plain\r\nContent-Length: ");
  head += body.length();
  head += F("\r\nConnection: close\r\n\r\n");
  client->setTimeout(kSendTimeoutMs);
  client->write(head.c_str(), head.length());
  client->write(body.c_str(), body.length());
  return ESP8266WebServer::CLIENT_MUST_STOP;
}

void handle() {
  server.client().setTimeout(kSendTimeoutMs);
  const int count = server.args();
  std::unique_ptr<ph::Arg[]> args(count > 0 ? new ph::Arg[count] : nullptr);
  for (int i = 0; i < count; ++i) {
    args[i] = ph::Arg{server.argName(i).c_str(), server.arg(i).c_str()};
  }
  const bool get = requestIsGet && server.method() == HTTP_GET;
  requestIsGet = false;
  const ph::Request req{get ? ph::Method::Get : ph::Method::Other,
                        server.uri().c_str(), args.get(), static_cast<size_t>(count)};
  // The context is read fresh for every request.
  const ph::Context ctx{gate->sessionLive(), gate->mode(), portal_motion::busy(), motionId,
                        ms::kTrimMin, ms::kTrimMax};
  const ph::Result r = ph::handle(req, ctx);
  if (r.page != ph::Page::None) {
    sendPage(r.page);
    return;
  }
  Glue glue;
  sendBody(ph::carryOut(r, glue));
}

}  // namespace

void begin(ms::OutputGate& outputGate, CalibrationStore& store) {
  gate = &outputGate;
  calibration = &store;
  static const char* const kRoutes[] = {"/",      "/controller", "/save",  "/editor",
                                        "/zero", "/setting",    "/online"};
  for (const char* route : kRoutes) server.on(route, HTTP_GET, handle);
  server.onNotFound(handle);  // other paths and methods: 404 from the handler
  server.addHook(screenMethod);
  server.begin();
}

void poll(uint32_t /*nowMs*/) { server.serve(); }

}  // namespace portal
