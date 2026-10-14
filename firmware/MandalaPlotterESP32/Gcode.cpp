#include "Gcode.h"
#include "Motion.h"
#include "Settings.h"

namespace {

int motionMode = 0;     // 0,1,2,3
bool absMode = true;    // G90
float feedRate = 1000;  // mm/min

struct Parsed {
  bool hasX = false, hasY = false, hasZ = false, hasI = false, hasJ = false, hasF = false, hasP = false, hasR = false;
  float x = 0, y = 0, z = 0, i = 0, j = 0, f = 0, p = 0;
  int g[8]; int gCount = 0;
  int m[4]; int mCount = 0;
  char comment[MSG_MAX + 40] = "";
  bool marker = false;
  int run = 0, step = 0, nail = -1;
};

bool parseLine(const char* line, Parsed& p, String& err) {
  const char* c = line;
  while (*c) {
    char ch = *c;
    if (ch == ';') {
      c++;
      while (*c == ' ') c++;
      strlcpy(p.comment, c, sizeof(p.comment));
      // trim trailing spaces / CR
      int n = strlen(p.comment);
      while (n > 0 && (p.comment[n - 1] == ' ' || p.comment[n - 1] == '\r')) p.comment[--n] = 0;
      break;
    }
    if (ch == '(') { while (*c && *c != ')') c++; if (*c) c++; continue; }
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') { c++; continue; }
    char letter = toupper(ch);
    if (letter < 'A' || letter > 'Z') { err = String("bad character '") + ch + "'"; return false; }
    c++;
    char* end;
    float v = strtof(c, &end);
    if (end == c) { err = String("number expected after ") + letter; return false; }
    c = end;
    switch (letter) {
      case 'G': {
        if (fabsf(v - roundf(v)) > 0.001f) { err = "unsupported G" + String(v, 1); return false; }
        if (p.gCount < 8) p.g[p.gCount++] = (int)lroundf(v);
        break;
      }
      case 'M':
        if (p.mCount < 4) p.m[p.mCount++] = (int)lroundf(v);
        break;
      case 'X': p.hasX = true; p.x = v; break;
      case 'Y': p.hasY = true; p.y = v; break;
      case 'Z': p.hasZ = true; p.z = v; break;
      case 'I': p.hasI = true; p.i = v; break;
      case 'J': p.hasJ = true; p.j = v; break;
      case 'F': p.hasF = true; p.f = v; break;
      case 'P': p.hasP = true; p.p = v; break;
      case 'R': p.hasR = true; break;
      case 'N': case 'S': case 'T': break;  // line numbers, spindle speed, tool: ignored
      default: err = String("unsupported word ") + letter; return false;
    }
  }
  if (strncmp(p.comment, "STEP ", 5) == 0) {
    p.marker = sscanf(p.comment, "STEP run=%d step=%d nail=%d", &p.run, &p.step, &p.nail) >= 2;
  }
  return true;
}

bool hasG(const Parsed& p, int code) {
  for (int k = 0; k < p.gCount; k++) if (p.g[k] == code) return true;
  return false;
}

bool inSoftLimits(float x, float y) {
  if (!S.softLimits || !Motion::homed()) return true;
  const float e = 0.001f;
  return x >= -e && y >= -e && x <= S.xMax + e && y <= S.yMax + e;
}

}  // namespace

void gcodeResetModal() {
  motionMode = 0;
  absMode = true;
  feedRate = 1000;
}

ExecResult gcodeExecute(const char* line, bool fromJob, uint32_t offset, String& err) {
  Parsed p;
  if (!parseLine(line, p, err)) return EX_ERROR;

  int newMotion = motionMode;
  for (int k = 0; k < p.gCount; k++) if (p.g[k] >= 0 && p.g[k] <= 3) newMotion = p.g[k];
  bool axisWords = p.hasX || p.hasY || p.hasZ;
  bool doMotion = axisWords || (newMotion >= 2 && (p.hasI || p.hasJ));
  bool marker = fromJob && p.marker;
  bool dwell = hasG(p, 4);

  int need = (marker ? 1 : 0) + (doMotion ? 1 : 0) + (dwell ? 1 : 0) + p.mCount;
  if (need > 0) {
    if (Motion::state() == M_ALARM) { err = "alarm lock: send $X or $H"; return EX_ERROR; }
    if (!Motion::canQueue(need)) return EX_BUSY;
  }
  if (doMotion && Motion::homingRequired()) { err = "home first ($H) or set $homing_enable=0"; return EX_ERROR; }

  // Validate G codes before changing any state.
  for (int k = 0; k < p.gCount; k++) {
    switch (p.g[k]) {
      case 0: case 1: case 2: case 3: case 4: case 17: case 21: case 28: case 54: case 90: case 91: case 94: break;
      case 20: err = "inches (G20) not supported"; return EX_ERROR;
      default: err = "unsupported G" + String(p.g[k]); return EX_ERROR;
    }
  }
  if (hasG(p, 28)) {
    if (fromJob) { err = "G28 not allowed inside a job"; return EX_ERROR; }
    if (!Motion::empty()) return EX_BUSY;
    Motion::homeAll();
    return EX_OK;
  }

  // Compute the target before touching modal state, so an error leaves nothing half-applied.
  bool absNow = hasG(p, 91) ? false : (hasG(p, 90) ? true : absMode);
  float feedNow = p.hasF ? p.f : feedRate;
  float px = Motion::plannedX(), py = Motion::plannedY(), pz = Motion::plannedZ();
  float tx = p.hasX ? (absNow ? p.x : px + p.x) : px;
  float ty = p.hasY ? (absNow ? p.y : py + p.y) : py;
  float tz = p.hasZ ? (absNow ? p.z : pz + p.z) : pz;

  float cx = 0, cy = 0, r = 0, a0 = 0, sweep = 0;
  if (doMotion) {
    if (newMotion >= 1 && feedNow <= 0) { err = "feed rate (F) missing"; return EX_ERROR; }
    if (!inSoftLimits(tx, ty)) { err = "soft limit: target outside travel"; return EX_ERROR; }
    if (newMotion >= 2) {
      if (p.hasR) { err = "arc R format not supported, use I/J"; return EX_ERROR; }
      cx = px + p.i; cy = py + p.j;
      r = hypotf(px - cx, py - cy);
      float r1 = hypotf(tx - cx, ty - cy);
      if (r < 1e-4f) { err = "arc radius is zero"; return EX_ERROR; }
      if (fabsf(r - r1) > 0.005f && fabsf(r - r1) > 0.001f * r) { err = "arc end point not on circle (error 33)"; return EX_ERROR; }
      a0 = atan2f(py - cy, px - cx);
      float a1 = atan2f(ty - cy, tx - cx);
      sweep = a1 - a0;
      if (newMotion == 2) { if (sweep >= -1e-6f) sweep -= TWO_PI; }   // clockwise
      else { if (sweep <= 1e-6f) sweep += TWO_PI; }                  // counter-clockwise
      if (!inSoftLimits(cx - r, cy - r) || !inSoftLimits(cx + r, cy + r)) { err = "soft limit: arc outside travel"; return EX_ERROR; }
    }
  }

  // Apply modal state.
  if (hasG(p, 90)) absMode = true;
  if (hasG(p, 91)) absMode = false;
  if (p.hasF) feedRate = p.f;
  motionMode = newMotion;

  if (marker) {
    Segment s{};
    s.type = SEG_MARK; s.run = p.run; s.step = p.step; s.nail = p.nail; s.offset = offset;
    Motion::queue(s);
  }
  if (dwell) {
    Segment s{};
    s.type = SEG_DWELL;
    s.dwellMs = p.hasP ? (uint32_t)(p.p * 1000.0f) : 0;
    Motion::queue(s);
  }
  if (doMotion) {
    if (motionMode == 0) Motion::queueMove(tx, ty, tz, S.maxRateXY, SEGF_RAPID);
    else if (motionMode == 1) Motion::queueMove(tx, ty, tz, feedRate, 0);
    else Motion::startArc(cx, cy, r, a0, sweep, tx, ty, tz, feedRate);
  }
  for (int k = 0; k < p.mCount; k++) {
    Segment s{};
    switch (p.m[k]) {
      case 0: case 1:
        s.type = SEG_PAUSE;
        strlcpy(s.msg, p.comment, sizeof(s.msg));
        Motion::queue(s);
        break;
      case 2: case 30:
        s.type = SEG_END;
        Motion::queue(s);
        break;
      case 17:
        s.type = SEG_MOTORS; s.dwellMs = 1;
        Motion::queue(s);
        break;
      case 18: case 84:
        s.type = SEG_MOTORS; s.dwellMs = 0;
        Motion::queue(s);
        break;
      case 3: case 4: case 5: case 7: case 8: case 9:
        break;  // spindle / coolant: ignored
      default:
        err = "unsupported M" + String(p.m[k]);
        return EX_ERROR;
    }
  }
  return EX_OK;
}

ExecResult gcodeJog(const char* body, String& err) {
  Parsed p;
  if (!parseLine(body, p, err)) return EX_ERROR;
  if (Motion::state() == M_ALARM) { err = "alarm lock: send $X or $H"; return EX_ERROR; }
  if (!(Motion::empty() || (Motion::state() == M_RUN && Motion::onlyJogging()))) { err = "busy"; return EX_ERROR; }
  if (!p.hasF || p.f <= 0) { err = "jog needs F"; return EX_ERROR; }
  if (!(p.hasX || p.hasY || p.hasZ)) { err = "jog needs X, Y or Z"; return EX_ERROR; }
  if (!Motion::canQueue(1)) return EX_BUSY;
  bool rel = hasG(p, 91) ? true : (hasG(p, 90) ? false : !absMode);
  float px = Motion::plannedX(), py = Motion::plannedY(), pz = Motion::plannedZ();
  float tx = p.hasX ? (rel ? px + p.x : p.x) : px;
  float ty = p.hasY ? (rel ? py + p.y : p.y) : py;
  float tz = p.hasZ ? (rel ? pz + p.z : p.z) : pz;
  if (S.softLimits && Motion::homed()) {   // clamp instead of refusing, nicer for jog buttons
    tx = constrain(tx, 0.0f, S.xMax);
    ty = constrain(ty, 0.0f, S.yMax);
  }
  Motion::queueMove(tx, ty, tz, p.f, SEGF_JOG);
  return EX_OK;
}
