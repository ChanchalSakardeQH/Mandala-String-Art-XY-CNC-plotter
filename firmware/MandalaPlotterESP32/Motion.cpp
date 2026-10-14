#include "Motion.h"
#include "Settings.h"
#include "Comms.h"
#include <FastAccelStepper.h>

namespace {

FastAccelStepperEngine engine;
FastAccelStepper* stX = nullptr;
FastAccelStepper* stY = nullptr;
FastAccelStepper* stY2 = nullptr;
FastAccelStepper* stZ = nullptr;

// ---------------- planner ring ----------------
Segment q[PLANNER_SIZE];
int qHead = 0, qCount = 0;

struct ArcGen {
  bool active = false;
  float cx, cy, r, a0, sweep, z0, z1, ex, ey, ez, feed;
  int n = 0, i = 0;
} arc;

float pX = 0, pY = 0, pZ = 0;   // planned position
float vZ = 0;                    // Z when there is no Z stepper (servo / none)

MachState st = M_IDLE;
MachState stBeforeHold = M_RUN;
bool stopping = false;           // abort in progress (decelerating)
bool isHomed = false;
bool safe = false;
int alarmNo = 0;
char pauseMsg[MSG_MAX] = "";

// ---------------- current segment ----------------
enum Stage : uint8_t { S_NONE, S_SERVO_WAIT, S_MOVING, S_DWELL, S_WAIT_START };
Stage stage = S_NONE;
Segment cur;
uint32_t waitUntil = 0;
bool servoKnown = false, servoUp = true;

// ---------------- limits (debounced) ----------------
struct Lim { int pin; uint8_t hist; } limX{PIN_LIM_X, 0}, limY{PIN_LIM_Y, 0}, limZ{PIN_LIM_Z, 0};

bool rawLimit(int pin) {
  bool low = digitalRead(pin) == LOW;
  return S.limitInvert ? !low : low;
}
void sampleLimit(Lim& l) { l.hist = (uint8_t)((l.hist << 1) | (rawLimit(l.pin) ? 1 : 0)); }
bool stableActive(const Lim& l) { return (l.hist & 0x07) == 0x07; }  // 3 samples in a row

// ---------------- helpers ----------------
inline int32_t toSteps(float mm, float spm) { return (int32_t)lroundf(mm * spm); }
bool zStepper() { return S.zMode == 1 && stZ != nullptr; }

bool anyRunning() {
  return (stX && stX->isRunning()) || (stY && stY->isRunning()) ||
         (stY2 && stY2->isRunning()) || (stZ && stZ->isRunning());
}

void stopAll(bool hard) {
  FastAccelStepper* all[] = {stX, stY, stY2, stZ};
  for (auto* s : all) if (s) { if (hard) s->forceStop(); else s->stopMove(); }
}

void servoWriteAngle(int deg) {
  uint32_t us = 500 + (uint32_t)constrain(deg, 0, 180) * 2000 / 180;
  uint32_t duty = (uint32_t)((uint64_t)us * 65535 / 20000);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_SERVO, duty);
#else
  ledcWrite(0, duty);
#endif
}

void setServo(bool up) {
  servoWriteAngle(up ? S.servoUp : S.servoDown);
  servoKnown = true;
  servoUp = up;
}

// Drive one axis toward targetSteps with a share of the path speed/accel.
bool axisMove(FastAccelStepper* s, int32_t target, float frac, float v, float a, float spm) {
  if (!s) return true;
  if (s->getCurrentPosition() == target) return true;
  uint32_t mhz = (uint32_t)max(1.0f, v * frac * spm * 1000.0f);
  int32_t acc = (int32_t)max(1.0f, a * frac * spm);
  s->setSpeedInMilliHz(mhz);
  s->setAcceleration(acc);
  return s->moveTo(target) == MOVE_OK;
}

// Starts (or restarts after a hold) the XY(Z) part of a move segment.
void issueMove(const Segment& sg) {
  float ax, ay, az;
  Motion::actual(ax, ay, az);
  float dx = sg.x - ax, dy = sg.y - ay;
  float dz = zStepper() ? sg.z - az : 0.0f;
  float len = sqrtf(dx * dx + dy * dy + dz * dz);
  if (!zStepper()) vZ = sg.z;
  if (len < 1e-5f) { stage = S_NONE; return; }

  float feed = (sg.flags & SEGF_RAPID) ? S.maxRateXY : min(sg.feed, S.maxRateXY);
  if (sg.flags & SEGF_ARC) feed = min(feed, S.arcRate);
  float v = feed / 60.0f;    // mm/s along the path
  float a = S.accelXY;       // mm/s^2 along the path
  if (dz != 0) {
    float zf = fabsf(dz) / len;
    if (v * zf > S.maxRateZ / 60.0f) v = (S.maxRateZ / 60.0f) / zf;
    if (a * zf > S.accelZ) a = S.accelZ / zf;
  }
  // Every axis gets speed and acceleration proportional to its share of the path,
  // so all axes follow the same ramp and the head moves in a straight line.
  bool ok = axisMove(stX, toSteps(sg.x, S.xSteps), fabsf(dx) / len, v, a, S.xSteps);
  ok &= axisMove(stY, toSteps(sg.y, S.ySteps), fabsf(dy) / len, v, a, S.ySteps);
  if (stY2) ok &= axisMove(stY2, toSteps(sg.y, S.ySteps), fabsf(dy) / len, v, a, S.ySteps);
  if (zStepper()) ok &= axisMove(stZ, toSteps(sg.z, S.zSteps), fabsf(dz) / len, v, a, S.zSteps);
  if (!ok) { Motion::alarm(5, "Stepper refused move"); return; }
  stage = S_MOVING;
}

void startSegment(const Segment& sg) {
  cur = sg;
  switch (sg.type) {
    case SEG_MOVE:
      if (S.zMode == 2) {
        bool up = sg.z > S.zSplit;
        if (!servoKnown || up != servoUp) {
          setServo(up);
          vZ = sg.z;
          waitUntil = millis() + S.servoMs;
          stage = S_SERVO_WAIT;
          return;
        }
      }
      issueMove(sg);
      break;
    case SEG_DWELL:
      waitUntil = millis() + sg.dwellMs;
      stage = S_DWELL;
      break;
    case SEG_MARK:
      jobOnMarker(sg);
      stage = S_NONE;
      break;
    case SEG_PAUSE:
      strlcpy(pauseMsg, sg.msg[0] ? sg.msg : "Paused (M0) - press Start", sizeof(pauseMsg));
      st = M_PAUSE;
      stage = S_WAIT_START;
      jobOnPause(sg);
      Comms::broadcastf("[MSG:PAUSE %s]", pauseMsg);
      break;
    case SEG_END:
      stage = S_NONE;
      jobOnEnd();
      break;
    case SEG_MOTORS:
      Motion::motorsEnable(sg.dwellMs != 0);
      stage = S_NONE;
      break;
  }
}

bool popSegment(Segment& out) {
  if (qCount == 0) return false;
  out = q[qHead];
  qHead = (qHead + 1) % PLANNER_SIZE;
  qCount--;
  return true;
}

void feedArc() {
  while (arc.active && qCount < PLANNER_SIZE) {
    arc.i++;
    Segment s{};
    s.type = SEG_MOVE;
    s.flags = SEGF_ARC;
    s.feed = arc.feed;
    if (arc.i >= arc.n) {
      s.x = arc.ex; s.y = arc.ey; s.z = arc.ez;
      arc.active = false;
    } else {
      float t = (float)arc.i / arc.n;
      float ang = arc.a0 + arc.sweep * t;
      s.x = arc.cx + arc.r * cosf(ang);
      s.y = arc.cy + arc.r * sinf(ang);
      s.z = arc.z0 + (arc.z1 - arc.z0) * t;
    }
    q[(qHead + qCount) % PLANNER_SIZE] = s;
    qCount++;
  }
}

// ---------------- homing ----------------
enum HStage : uint8_t { H_OFF, H_AXIS, H_PRE_WAIT, H_SEEK, H_BACK_WAIT, H_LOCATE, H_FINAL_WAIT };
HStage hs = H_OFF;
char hAxes[3];
int hCount = 0, hIdx = 0;
int32_t hStartPos = 0;

struct HomeAxis { FastAccelStepper* s; FastAccelStepper* mirror; float spm; float maxT; Lim* lim; bool toMax; };

HomeAxis homeAxis(char a) {
  switch (a) {
    case 'X': return {stX, nullptr, S.xSteps, S.xMax, &limX, (S.homeDirMask & 1) != 0};
    case 'Y': return {stY, stY2, S.ySteps, S.yMax, &limY, (S.homeDirMask & 2) != 0};
    default:  return {stZ, nullptr, S.zSteps, S.zMax, &limZ, (S.homeDirMask & 4) != 0};
  }
}

void hMove(const HomeAxis& h, int32_t delta, float rate) {
  FastAccelStepper* both[] = {h.s, h.mirror};
  for (auto* s : both) if (s) {
    s->setSpeedInMilliHz((uint32_t)(rate / 60.0f * h.spm * 1000.0f));
    s->setAcceleration((int32_t)(S.accelXY * h.spm));
    s->move(delta);
  }
}
void hRun(const HomeAxis& h, float rate) {
  FastAccelStepper* both[] = {h.s, h.mirror};
  for (auto* s : both) if (s) {
    s->setSpeedInMilliHz((uint32_t)(rate / 60.0f * h.spm * 1000.0f));
    s->setAcceleration((int32_t)(S.accelXY * h.spm * 4));
    if (h.toMax) s->runForward(); else s->runBackward();
  }
}
void hStop(const HomeAxis& h) { h.s->forceStop(); if (h.mirror) h.mirror->forceStop(); }
bool hRunning(const HomeAxis& h) { return h.s->isRunning() || (h.mirror && h.mirror->isRunning()); }

void homingFail(const char* why) {
  stopAll(true);
  hs = H_OFF;
  Motion::alarm(9, why);
}

void serviceHoming() {
  if (hs == H_OFF) return;
  HomeAxis h = homeAxis(hAxes[hIdx]);
  int32_t away = (h.toMax ? -1 : 1) * toSteps(S.homingPulloff, h.spm);
  switch (hs) {
    case H_AXIS:
      if (!h.s) { homingFail("Homing: axis not configured"); return; }
      if (rawLimit(h.lim->pin)) { hMove(h, away, S.homingSeek); hs = H_PRE_WAIT; }
      else { hStartPos = h.s->getCurrentPosition(); hRun(h, S.homingSeek); hs = H_SEEK; }
      break;
    case H_PRE_WAIT:
      if (!hRunning(h)) {
        if (rawLimit(h.lim->pin)) { homingFail("Homing: switch stays pressed"); return; }
        hStartPos = h.s->getCurrentPosition(); hRun(h, S.homingSeek); hs = H_SEEK;
      }
      break;
    case H_SEEK:
      if (rawLimit(h.lim->pin)) { hStop(h); hMove(h, away, S.homingSeek); hs = H_BACK_WAIT; }
      else if (labs(h.s->getCurrentPosition() - hStartPos) > toSteps(h.maxT * 1.5f, h.spm)) homingFail("Homing: switch not found");
      break;
    case H_BACK_WAIT:
      if (!hRunning(h)) { hStartPos = h.s->getCurrentPosition(); hRun(h, S.homingFeed); hs = H_LOCATE; }
      break;
    case H_LOCATE:
      if (rawLimit(h.lim->pin)) {
        hStop(h);
        int32_t zero = h.toMax ? toSteps(h.maxT, h.spm) : 0;
        h.s->setCurrentPosition(zero);
        if (h.mirror) h.mirror->setCurrentPosition(zero);
        hMove(h, away, S.homingSeek);
        hs = H_FINAL_WAIT;
      } else if (labs(h.s->getCurrentPosition() - hStartPos) > toSteps(S.homingPulloff * 5, h.spm)) {
        homingFail("Homing: switch lost during locate");
      }
      break;
    case H_FINAL_WAIT:
      if (!hRunning(h)) {
        if (++hIdx < hCount) { hs = H_AXIS; break; }
        hs = H_OFF;
        isHomed = true;
        float x, y, z; Motion::actual(x, y, z);
        pX = x; pY = y; pZ = z;
        st = M_IDLE;
        Comms::broadcast("[MSG:Homed]");
      }
      break;
    default: break;
  }
}

FastAccelStepper* makeStepper(int stepPin, int dirPin, int inv) {
  FastAccelStepper* s = engine.stepperConnectToPin(stepPin);
  if (!s) return nullptr;
  s->setDirectionPin(dirPin, inv == 0);
  return s;
}

}  // namespace

namespace Motion {

void begin(bool safeMode) {
  safe = safeMode;
  pinMode(PIN_EN, OUTPUT);
  motorsEnable(!safe);   // safe mode: drivers disabled, no holding current
  pinMode(PIN_LIM_X, INPUT);   // GPIO34/35/39 have no internal pull-ups: fit 10k to 3V3
  pinMode(PIN_LIM_Y, INPUT);
  pinMode(PIN_LIM_Z, INPUT);

  engine.init();
  stX = makeStepper(PIN_X_STEP, PIN_X_DIR, S.xInv);
  stY = makeStepper(PIN_Y_STEP, PIN_Y_DIR, S.yInv);
  if (S.yDual) stY2 = makeStepper(PIN_Y2_STEP, PIN_Y2_DIR, S.y2Inv);
  if (S.zMode == 1) stZ = makeStepper(PIN_Z_STEP, PIN_Z_DIR, S.zInv);
  if (!stX || !stY || (S.yDual && !stY2) || (S.zMode == 1 && !stZ)) alarm(1, "Stepper init failed");

  if (S.zMode == 2) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(PIN_SERVO, 50, 16);
#else
    ledcSetup(0, 50, 16);
    ledcAttachPin(PIN_SERVO, 0);
#endif
    // No pulse at power-up: the servo stays limp until the first Z move or homing,
    // so it can't add a current spike while Wi-Fi and Bluetooth start.
  }
  // At power-up Z is assumed to be up (z_safe).
  pZ = vZ = S.zSafe;
  if (stZ) stZ->setCurrentPosition(toSteps(S.zSafe, S.zSteps));
  if (safe) {
    st = M_ALARM;
    alarmNo = 12;
  } else if (alarmNo == 0 && homingRequired()) {
    st = M_ALARM;
    alarmNo = 11;
  }
}

void service() {
  sampleLimit(limX); sampleLimit(limY); sampleLimit(limZ);

  if (st == M_HOMING) { serviceHoming(); return; }

  // Hard limits: any switch closing while moving is a crash.
  if (S.hardLimits && (st == M_RUN || st == M_HOLD) && anyRunning()) {
    bool zLim = S.zMode == 1 && S.homeZ;
    if (stableActive(limX) || stableActive(limY) || (zLim && stableActive(limZ))) {
      stopAll(true);
      alarm(1, "Hard limit triggered");
      return;
    }
  }

  if (stopping) {
    if (!anyRunning()) {
      stopping = false;
      float x, y, z; actual(x, y, z);
      pX = x; pY = y; pZ = zStepper() ? z : vZ;
      if (st != M_ALARM) st = M_IDLE;
    }
    return;
  }
  if (st == M_ALARM || st == M_HOLD || st == M_PAUSE) return;

  feedArc();

  // finish current stage
  switch (stage) {
    case S_SERVO_WAIT:
      if ((int32_t)(millis() - waitUntil) < 0) return;
      issueMove(cur);
      if (stage != S_NONE) return;
      break;
    case S_MOVING:
      if (anyRunning()) return;
      stage = S_NONE;
      break;
    case S_DWELL:
      if ((int32_t)(millis() - waitUntil) < 0) return;
      stage = S_NONE;
      break;
    case S_WAIT_START:
      return;
    default: break;
  }

  // start following segments (non-motion ones complete instantly)
  Segment next;
  while (stage == S_NONE && st != M_ALARM && st != M_PAUSE) {
    if (!popSegment(next)) { if (st == M_RUN) st = M_IDLE; break; }
    st = M_RUN;
    startSegment(next);
    feedArc();
  }
}

bool canQueue(int n) { return st != M_ALARM && !stopping && !arc.active && qCount + n <= PLANNER_SIZE; }

bool queue(const Segment& s) {
  if (qCount >= PLANNER_SIZE) return false;
  q[(qHead + qCount) % PLANNER_SIZE] = s;
  qCount++;
  if (s.type == SEG_MOVE) { pX = s.x; pY = s.y; pZ = s.z; }
  return true;
}

bool queueMove(float x, float y, float z, float feed, uint8_t flags) {
  Segment s{};
  s.type = SEG_MOVE; s.x = x; s.y = y; s.z = z; s.feed = feed; s.flags = flags;
  return queue(s);
}

bool arcBusy() { return arc.active; }

void startArc(float cx, float cy, float r, float a0, float sweep, float ex, float ey, float ez, float feed) {
  // chord length for the requested tolerance, 0.05..1 mm
  float seg = 2.0f * sqrtf(max(1e-6f, 2.0f * r * S.arcTol));
  seg = constrain(seg, 0.05f, 1.0f);
  int n = max(1, (int)ceilf(fabsf(sweep) * r / seg));
  arc.cx = cx; arc.cy = cy; arc.r = r; arc.a0 = a0; arc.sweep = sweep;
  arc.z0 = pZ; arc.z1 = ez; arc.ex = ex; arc.ey = ey; arc.ez = ez; arc.feed = feed;
  arc.n = n; arc.i = 0; arc.active = true;
  pX = ex; pY = ey; pZ = ez;
  feedArc();
}

bool empty() { return qCount == 0 && !arc.active && stage == S_NONE && !anyRunning() && !stopping; }

bool onlyJogging() {
  if (arc.active) return false;
  if (stage != S_NONE && !(cur.type == SEG_MOVE && (cur.flags & SEGF_JOG))) return false;
  for (int i = 0; i < qCount; i++) {
    const Segment& s = q[(qHead + i) % PLANNER_SIZE];
    if (!(s.type == SEG_MOVE && (s.flags & SEGF_JOG))) return false;
  }
  return true;
}

float plannedX() { return pX; }
float plannedY() { return pY; }
float plannedZ() { return pZ; }

void actual(float& x, float& y, float& z) {
  x = stX ? stX->getCurrentPosition() / S.xSteps : 0;
  y = stY ? stY->getCurrentPosition() / S.ySteps : 0;
  z = zStepper() ? stZ->getCurrentPosition() / S.zSteps : vZ;
}

void feedHold() {
  if (st != M_RUN) return;
  if (onlyJogging()) { abort(); return; }   // '!' cancels a jog
  if (stage == S_MOVING) stopAll(false);
  stBeforeHold = st;
  st = M_HOLD;
  Comms::broadcast("[MSG:Hold]");
}

void cycleStart() {
  if (st == M_HOLD) {
    if (anyRunning()) return;  // still decelerating; press again
    st = M_RUN;
    if (stage == S_MOVING) issueMove(cur);          // continue the same straight line
    else if (stage == S_DWELL) waitUntil = millis() + cur.dwellMs;
  } else if (st == M_PAUSE) {
    st = M_RUN;
    stage = S_NONE;
    pauseMsg[0] = 0;
  }
}

void abort() {
  if (st == M_HOMING) { stopAll(true); hs = H_OFF; isHomed = false; st = M_IDLE; }
  qCount = 0; qHead = 0;
  arc.active = false;
  stage = S_NONE;
  pauseMsg[0] = 0;
  if (anyRunning()) { stopAll(false); stopping = true; }
  else { float x, y, z; actual(x, y, z); pX = x; pY = y; pZ = zStepper() ? z : vZ; }
  if (st != M_ALARM) st = stopping ? M_HOLD : M_IDLE;
}

void homeAll() {
  if (safe) { Comms::broadcast("[MSG:Safe mode - fix the power supply, then $REBOOT]"); return; }
  if (st == M_RUN || st == M_HOLD || st == M_PAUSE || st == M_HOMING) return;
  qCount = 0; qHead = 0; arc.active = false; stage = S_NONE;
  hCount = 0;
  if (S.zMode == 1 && S.homeZ) hAxes[hCount++] = 'Z';
  else if (S.zMode == 2) { setServo(true); vZ = S.zSafe; }
  else vZ = S.zSafe;
  hAxes[hCount++] = 'X';
  hAxes[hCount++] = 'Y';
  hIdx = 0;
  hs = H_AXIS;
  alarmNo = 0;
  isHomed = false;
  st = M_HOMING;
  Comms::broadcast("[MSG:Homing]");
}

void alarm(int code, const char* msg) {
  qCount = 0; qHead = 0; arc.active = false; stage = S_NONE;
  stopAll(true);
  alarmNo = code;
  st = M_ALARM;
  if (code == 1 || code == 9) isHomed = false;   // position can no longer be trusted
  Comms::broadcastf("ALARM:%d %s", code, msg);
}

bool clearAlarm() {
  if (st != M_ALARM) return true;
  if (safe) return false;
  if (anyRunning()) return false;
  alarmNo = 0;
  st = M_IDLE;
  float x, y, z; actual(x, y, z);
  pX = x; pY = y; pZ = zStepper() ? z : vZ;
  return true;
}

void motorsEnable(bool on) {
  if (safe) on = false;
  bool level = on ? LOW : HIGH;
  if (S.enableInvert) level = !level;
  digitalWrite(PIN_EN, level);
  if (!on) isHomed = false;   // motors can be moved by hand
}

MachState state() { return st; }

const char* stateName() {
  switch (st) {
    case M_IDLE: return "Idle";
    case M_RUN: return "Run";
    case M_HOLD: return "Hold";
    case M_PAUSE: return "Pause";
    case M_HOMING: return "Home";
    default: return "Alarm";
  }
}

bool homed() { return isHomed; }
bool homingRequired() { return S.homingEnable && !isHomed; }
const char* pauseMessage() { return pauseMsg; }
int alarmCode() { return alarmNo; }

bool limitActive(char axis) {
  switch (axis) {
    case 'X': return rawLimit(PIN_LIM_X);
    case 'Y': return rawLimit(PIN_LIM_Y);
    default: return rawLimit(PIN_LIM_Z);
  }
}

}  // namespace Motion
