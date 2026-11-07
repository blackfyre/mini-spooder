#include "portal/request_handler.h"

#include <stdio.h>
#include <string.h>

namespace ms {
namespace portal {

bool parseInteger(const char* text, int32_t& out) {
  if (text == nullptr) return false;
  const size_t len = strlen(text);
  if (len == 0 || len > kMaxIntegerText) return false;
  size_t i = 0;
  bool negative = false;
  if (text[0] == '+' || text[0] == '-') {
    negative = text[0] == '-';
    i = 1;
  }
  if (i == len) return false;
  int64_t value = 0;
  for (; i < len; ++i) {
    const char c = text[i];
    if (c < '0' || c > '9') return false;
    value = value * 10 + (c - '0');
    if (value > 2147483648LL) return false;  // stays bounded; the range check follows
  }
  if (negative) value = -value;
  if (value < INT32_MIN || value > INT32_MAX) return false;
  out = static_cast<int32_t>(value);
  return true;
}

namespace {

// The first argument with this name, or nullptr.
const char* findArg(const Request& r, const char* name) {
  for (size_t i = 0; i < r.argCount; ++i) {
    if (r.args[i].name != nullptr && strcmp(r.args[i].name, name) == 0) {
      return r.args[i].value != nullptr ? r.args[i].value : "";
    }
  }
  return nullptr;
}

bool intInRange(const char* text, int32_t low, int32_t high, int32_t& out) {
  return parseInteger(text, out) && out >= low && out <= high;
}

Result base(int status, ContentType type) {
  Result r;
  memset(&r, 0, sizeof r);
  r.status = status;
  r.type = type;
  r.page = Page::None;
  r.action.kind = ActionKind::None;
  r.head[0] = '\0';
  r.echo = nullptr;
  r.tail = nullptr;
  return r;
}

Result text(int status, ContentType type, const char* body) {
  Result r = base(status, type);
  snprintf(r.head, sizeof r.head, "%s", body);
  return r;
}

Result plain(int status, const char* body) { return text(status, ContentType::Plain, body); }

Result badRequest() { return plain(400, "bad request"); }

Result page(Page p) {
  Result r = base(200, ContentType::Html);
  r.page = p;
  return r;
}

Result notFound(const char* path) {
  Result r = plain(404, "Not found: ");
  r.echo = path;
  return r;
}

Result pmEcho(const char* pm) {
  Result r = text(200, ContentType::Html, "(pm)=(");
  r.echo = pm;
  r.tail = ")";
  return r;
}

Result controller(const Request& req, const Context& ctx) {
  const char* pm = findArg(req, "pm");
  const char* servo = findArg(req, "servo");
  if (pm == nullptr && servo == nullptr) {
    // Only pms, or nothing: no effect, as in the stock image.
    return text(200, ContentType::Html, "(pm)=()");
  }
  if (ctx.sessionLive) return plain(409, kRemoteInControl);
  if (ctx.mode == OutputMode::Fault) return plain(503, kServoOutputFault);
  if (pm != nullptr && servo != nullptr) return badRequest();

  if (pm != nullptr) {
    int32_t id;
    if (!parseInteger(pm, id)) return badRequest();
    Result r = pmEcho(pm);
    if (ctx.isMotionId != nullptr && ctx.isMotionId(id)) {
      r.action.kind = ActionKind::RequestMotion;
      r.action.motion = id;
    }
    return r;
  }

  int32_t index, angle;
  if (!intInRange(servo, 0, kServoCount - 1, index)) return badRequest();
  if (!intInRange(findArg(req, "value"), kAngleMin, kAngleMax, angle)) return badRequest();
  Result r = text(200, ContentType::Html, "(pm)=()");
  r.action.kind = ActionKind::DirectServo;
  r.action.servo = index;
  r.action.value = angle;
  return r;
}

Result save(const Request& req, const Context& ctx) {
  if (ctx.sessionLive) return plain(409, kRemoteInControl);
  if (ctx.motionBusy) return plain(409, kMotionPlaying);
  int32_t key;
  if (!parseInteger(findArg(req, "key"), key)) return badRequest();
  if (key == 100) {
    Result r = text(200, ContentType::Html, "(key, value)=(100,0)");
    r.action.kind = ActionKind::ResetTrims;
    return r;
  }
  if (key < 0 || key >= kServoCount) return badRequest();
  int32_t value;
  if (!intInRange(findArg(req, "value"), ctx.trimMin, ctx.trimMax, value)) return badRequest();
  Result r = base(200, ContentType::Html);
  snprintf(r.head, sizeof r.head, "(key, value)=(%d,%d)", static_cast<int>(key),
           static_cast<int>(value));
  r.action.kind = ActionKind::StoreTrim;
  r.action.servo = key;
  r.action.value = value;
  return r;
}

Result online(const Request& req, const Context& ctx) {
  if (ctx.sessionLive) return plain(409, kRemoteInControl);
  if (ctx.mode == OutputMode::Fault) return plain(503, kServoOutputFault);
  static const char* const kNames[kServoCount] = {"m0", "m1", "m2", "m3",
                                                  "m4", "m5", "m6", "m7"};
  Result r = base(200, ContentType::Html);
  for (int i = 0; i < kServoCount; ++i) {
    int32_t a;
    if (!intInRange(findArg(req, kNames[i]), kAngleMin, kAngleMax, a)) return badRequest();
    r.action.angles[i] = static_cast<uint8_t>(a);
  }
  int32_t t;
  if (!intInRange(findArg(req, "t1"), 0, 60000, t)) return badRequest();
  r.action.kind = ActionKind::PlayKeyframe;
  r.action.durationMs = static_cast<uint16_t>(t);
  snprintf(r.head, sizeof r.head, "(m0, m1)=(%u,%u)", static_cast<unsigned>(r.action.angles[0]),
           static_cast<unsigned>(r.action.angles[1]));
  return r;
}

}  // namespace

Result handle(const Request& req, const Context& ctx) {
  const char* path = req.path != nullptr ? req.path : "";
  if (req.method != Method::Get) return notFound(path);
  if (strcmp(path, "/") == 0) return page(Page::Controller);
  if (strcmp(path, "/zero") == 0) return page(Page::Zero);
  if (strcmp(path, "/setting") == 0) return page(Page::Setting);
  if (strcmp(path, "/editor") == 0) return page(Page::Editor);
  if (strcmp(path, "/controller") == 0) return controller(req, ctx);
  if (strcmp(path, "/save") == 0) return save(req, ctx);
  if (strcmp(path, "/online") == 0) return online(req, ctx);
  return notFound(path);
}

Result carryOut(const Result& r, Executor& ex) {
  const Action& a = r.action;
  switch (a.kind) {
    case ActionKind::None:
      return r;
    case ActionKind::RequestMotion:
      ex.requestMotion(a.motion);
      return r;
    case ActionKind::DirectServo:
      ex.stopMotion();
      return ex.directServo(a.servo, a.value) ? r : directServoRejected();
    case ActionKind::StoreTrim:
      return ex.storeTrim(a.servo, a.value) ? r : saveFailed();
    case ActionKind::ResetTrims:
      return ex.resetTrims() ? r : saveFailed();
    case ActionKind::PlayKeyframe:
      return ex.playKeyframe(a.angles, a.durationMs) ? r : keyframeRejected();
  }
  return r;
}

Result directServoRejected() { return plain(503, kServoOutputOff); }

Result keyframeRejected() { return badRequest(); }

Result saveFailed() { return plain(500, kSaveFailed); }

size_t formatBody(const Result& r, char* buf, size_t size) {
  if (size == 0) return 0;
  const int n = snprintf(buf, size, "%s%s%s", r.head, r.echo != nullptr ? r.echo : "",
                         r.tail != nullptr ? r.tail : "");
  if (n < 0) {
    buf[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n) < size ? static_cast<size_t>(n) : size - 1;
}

}  // namespace portal
}  // namespace ms
