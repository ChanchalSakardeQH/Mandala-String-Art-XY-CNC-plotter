#include "Job.h"
#include "Config.h"
#include "Motion.h"
#include "Gcode.h"
#include "Settings.h"
#include "Comms.h"
#include <LittleFS.h>
#include <Preferences.h>

namespace {

File file;
bool running = false;
char jobName[32] = "";
uint32_t fileSize = 1;
uint32_t lineNo = 0;
char line[CMD_LINE_MAX];
bool pending = false;
uint32_t pendingOffset = 0;
bool eof = false;

// progress from the last marker the machine actually reached
int curRun = -1, curStep = -1, curNail = -1, prevNail = -1;
uint32_t curOffset = 0;
float mX = 0, mY = 0, mZ = 0;
bool haveMarker = false;
uint32_t lastSave = 0;

enum WaitHome : uint8_t { W_NONE, W_START, W_RESUME } waitHome = W_NONE;

struct Prog {
  bool valid = false;
  char file[32] = "";
  uint32_t offset = 0;
  float x = 0, y = 0, z = 0;
  int run = 0, step = 0, nail = -1, prev = -1;
} saved;

Preferences prefs;

String pathOf(const char* name) { return String(JOB_DIR) + "/" + name; }

void loadProgress() {
  prefs.begin("prog", true);
  saved.valid = prefs.getBool("valid", false);
  prefs.getString("file", saved.file, sizeof(saved.file));
  saved.offset = prefs.getUInt("offset", 0);
  saved.x = prefs.getFloat("x", 0);
  saved.y = prefs.getFloat("y", 0);
  saved.z = prefs.getFloat("z", 0);
  saved.run = prefs.getInt("run", 0);
  saved.step = prefs.getInt("step", 0);
  saved.nail = prefs.getInt("nail", -1);
  saved.prev = prefs.getInt("prev", -1);
  prefs.end();
}

void saveProgress() {
  if (!haveMarker) return;
  saved.valid = true;
  strlcpy(saved.file, jobName, sizeof(saved.file));
  saved.offset = curOffset;
  saved.x = mX; saved.y = mY; saved.z = mZ;
  saved.run = curRun; saved.step = curStep; saved.nail = curNail; saved.prev = prevNail;
  prefs.begin("prog", false);
  prefs.putBool("valid", true);
  prefs.putString("file", saved.file);
  prefs.putUInt("offset", saved.offset);
  prefs.putFloat("x", saved.x);
  prefs.putFloat("y", saved.y);
  prefs.putFloat("z", saved.z);
  prefs.putInt("run", saved.run);
  prefs.putInt("step", saved.step);
  prefs.putInt("nail", saved.nail);
  prefs.putInt("prev", saved.prev);
  prefs.end();
  lastSave = millis();
}

bool openAt(const char* name, uint32_t offset, String& err) {
  file = LittleFS.open(pathOf(name), "r");
  if (!file) { err = "file not found"; return false; }
  fileSize = max((uint32_t)1, (uint32_t)file.size());
  if (offset > 0 && !file.seek(offset)) { file.close(); err = "seek failed"; return false; }
  strlcpy(jobName, name, sizeof(jobName));
  running = true;
  eof = false;
  pending = false;
  lineNo = 0;
  haveMarker = false;
  curOffset = offset;
  gcodeResetModal();
  return true;
}

bool readLine(String& err) {
  if (!file.available()) { eof = true; return false; }
  pendingOffset = file.position();
  int n = 0;
  bool overflow = false;
  while (file.available()) {
    int c = file.read();
    if (c == '\n') break;
    if (c == '\r') continue;
    if (n < CMD_LINE_MAX - 1) line[n++] = (char)c; else overflow = true;
  }
  line[n] = 0;
  lineNo++;
  if (overflow) { err = "line too long"; return false; }
  pending = true;
  return true;
}

bool machineBusy() {
  MachState s = Motion::state();
  return s == M_RUN || s == M_HOLD || s == M_PAUSE || s == M_HOMING || !Motion::empty();
}

bool doResume(String& err) {
  if (!openAt(saved.file, saved.offset, err)) return false;
  curRun = saved.run; curStep = saved.step; curNail = saved.prev; prevNail = saved.prev;
  // Lift, travel to where the machine was when it reached the saved step, lower, wait for the operator.
  Motion::queueMove(Motion::plannedX(), Motion::plannedY(), S.zSafe, S.maxRateZ, SEGF_RAPID);
  Motion::queueMove(saved.x, saved.y, S.zSafe, S.maxRateXY, SEGF_RAPID);
  Motion::queueMove(saved.x, saved.y, saved.z, S.maxRateZ, 0);
  Segment p{};
  p.type = SEG_PAUSE;
  if (saved.step > 0 && saved.prev >= 0) snprintf(p.msg, sizeof(p.msg), "Tie thread at nail %d, Start", saved.prev);
  else snprintf(p.msg, sizeof(p.msg), "Resume run %d, Start", saved.run);
  Motion::queue(p);
  Comms::broadcastf("[MSG:Resuming %s run=%d step=%d]", saved.file, saved.run, saved.step);
  return true;
}

}  // namespace

namespace Job {

void begin() {
  loadProgress();
}

void service() {
  if (waitHome != W_NONE) {
    MachState s = Motion::state();
    if (s == M_HOMING) return;
    WaitHome w = waitHome;
    waitHome = W_NONE;
    if (s == M_IDLE && Motion::homed()) {
      String err;
      bool ok = (w == W_START) ? openAt(jobName, 0, err) : doResume(err);
      if (!ok) Comms::broadcastf("[MSG:Job not started: %s]", err.c_str());
      else if (w == W_START) Comms::broadcastf("[MSG:Job started %s]", jobName);
    } else {
      Comms::broadcast("[MSG:Job cancelled: homing failed]");
    }
    return;
  }
  if (!running) return;
  if (Motion::state() == M_ALARM) { stop("alarm"); return; }

  for (int k = 0; k < 16; k++) {
    String err;
    if (!pending) {
      if (eof) break;
      if (!readLine(err)) {
        if (err.length()) { Comms::broadcastf("error: job line %lu: %s", (unsigned long)lineNo, err.c_str()); stop("bad line"); return; }
        break;
      }
    }
    ExecResult r = gcodeExecute(line, true, pendingOffset, err);
    if (r == EX_BUSY) break;
    pending = false;
    if (r == EX_ERROR) {
      Comms::broadcastf("error: job line %lu: %s", (unsigned long)lineNo, err.c_str());
      stop("G-code error");
      return;
    }
  }
  if (running && eof && !pending && Motion::empty() && Motion::state() == M_IDLE) jobOnEnd();
}

bool start(const char* name, String& err) {
  if (running || waitHome != W_NONE) { err = "a job is already running"; return false; }
  if (machineBusy()) { err = "machine busy"; return false; }
  if (!LittleFS.exists(pathOf(name))) { err = "file not found"; return false; }
  if (Motion::state() == M_ALARM && !S.homingEnable) { err = "alarm lock: send $X"; return false; }
  if (Motion::homingRequired() || Motion::state() == M_ALARM) {
    strlcpy(jobName, name, sizeof(jobName));
    waitHome = W_START;
    Motion::homeAll();
    return true;
  }
  if (!openAt(name, 0, err)) return false;
  Comms::broadcastf("[MSG:Job started %s]", name);
  return true;
}

bool resume(String& err) {
  if (running || waitHome != W_NONE) { err = "a job is already running"; return false; }
  if (!saved.valid) { err = "no saved progress"; return false; }
  if (!LittleFS.exists(pathOf(saved.file))) { err = "saved job file is missing"; return false; }
  if (machineBusy()) { err = "machine busy"; return false; }
  if (Motion::state() == M_ALARM && !S.homingEnable) { err = "alarm lock: send $X"; return false; }
  if (Motion::homingRequired() || Motion::state() == M_ALARM) {
    waitHome = W_RESUME;
    Motion::homeAll();
    return true;
  }
  return doResume(err);
}

void stop(const char* why) {
  if (waitHome != W_NONE) waitHome = W_NONE;
  if (!running) { Motion::abort(); return; }
  running = false;
  file.close();
  if (Motion::state() != M_ALARM) Motion::abort();
  saveProgress();
  Comms::broadcastf("[MSG:Job stopped (%s) at run=%d step=%d - $RESUME to continue]", why, curRun, curStep);
}

bool active() { return running; }
bool waitingForHoming() { return waitHome != W_NONE; }
const char* name() { return jobName; }
float percent() { return running ? 100.0f * curOffset / fileSize : 0.0f; }
int run() { return curRun; }
int step() { return curStep; }
int nail() { return curNail; }
bool hasSavedProgress() { return saved.valid; }

String savedProgressText() {
  if (!saved.valid) return "";
  char buf[96];
  snprintf(buf, sizeof(buf), "[PROG:%s,%d,%d,%d]", saved.file, saved.run, saved.step, saved.nail);
  return String(buf);
}

void clearProgress() {
  saved.valid = false;
  prefs.begin("prog", false);
  prefs.clear();
  prefs.end();
}

bool isJobFile(const char* n) { return running && strcmp(n, jobName) == 0; }
const char* savedFile() { return saved.valid ? saved.file : ""; }

}  // namespace Job

void jobOnMarker(const Segment& s) {
  if (!running) return;
  prevNail = curNail;
  curRun = s.run; curStep = s.step; curNail = s.nail; curOffset = s.offset;
  Motion::actual(mX, mY, mZ);
  haveMarker = true;
  if (s.step == 0 || millis() - lastSave > 2000) saveProgress();
}

void jobOnPause(const Segment&) {
  if (running) saveProgress();
}

void jobOnEnd() {
  if (!running) { Comms::broadcast("[MSG:Program end]"); return; }
  running = false;
  file.close();
  Job::clearProgress();
  Comms::broadcastf("[MSG:Job complete %s]", jobName);
}
