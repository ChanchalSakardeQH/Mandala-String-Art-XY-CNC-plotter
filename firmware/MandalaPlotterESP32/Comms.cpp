#include "Comms.h"
#include "Config.h"
#include "Settings.h"
#include "Motion.h"
#include "Gcode.h"
#include "Job.h"
#include <WiFi.h>
#include <esp_idf_version.h>
#include <ESPmDNS.h>
#include <BluetoothSerial.h>
#include <LittleFS.h>
#include <stdarg.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error "Bluetooth Classic is not available: select board 'ESP32 Dev Module' (ESP32-WROOM-32), not S2/S3/C3"
#endif

namespace {

BluetoothSerial bt;
WiFiServer server(TCP_PORT);
WiFiClient tcp;
bool btUp = false;
volatile uint32_t wifiJoins = 0, wifiLeaves = 0;
uint32_t joinsShown = 0, leavesShown = 0;

struct Chan {
  Stream* s = nullptr;
  char buf[CMD_LINE_MAX];
  int len = 0;
  bool overflow = false;
  bool pending = false;     // buf holds a line waiting for planner space
  bool uploading = false;
  File up;
  uint32_t upLines = 0, upBytes = 0;
};
Chan chans[3];  // 0 = USB, 1 = Bluetooth, 2 = Wi-Fi TCP

// Web dashboard: commands arrive over HTTP; replies are captured into a String.
class Capture : public Stream {
 public:
  String out;
  int available() { return 0; }
  int read() { return -1; }
  int peek() { return -1; }
  void flush() {}
  size_t write(uint8_t c) { if (out.length() < 6000) out += (char)c; return 1; }
};
Capture capture;
Chan webChan;

// Last broadcast messages, so the dashboard can show events it missed between polls.
const int EV_COUNT = 24;
struct Ev { uint32_t seq; char text[112]; };
Ev events[EV_COUNT];
uint32_t evSeq = 0;
void pushEvent(const char* line) {
  evSeq++;
  Ev& e = events[(evSeq - 1) % EV_COUNT];
  e.seq = evSeq;
  strlcpy(e.text, line, sizeof(e.text));
}

bool connected(int i) {
  if (i == 0) return true;
  if (i == 1) return btUp && bt.hasClient();
  return tcp && tcp.connected();
}

void reply(Chan& c, const char* text) { c.s->print(text); c.s->print("\r\n"); }
void replyf(Chan& c, const char* fmt, ...) {
  char b[192];
  va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
  reply(c, b);
}
void ok(Chan& c) { reply(c, "ok"); }
void fail(Chan& c, const String& msg) { replyf(c, "error: %s", msg.c_str()); }

bool validName(const char* n) {
  size_t len = strlen(n);
  if (len == 0 || len > 31 || n[0] == '.') return false;
  for (size_t i = 0; i < len; i++) {
    char ch = n[i];
    if (!(isalnum((unsigned char)ch) || ch == '.' || ch == '_' || ch == '-')) return false;
  }
  return true;
}

String jobPath(const char* n) { return String(JOB_DIR) + "/" + n; }

void sendStatus(Chan& c) {
  float x, y, z;
  Motion::actual(x, y, z);
  const char* state = Motion::stateName();
  if (Job::waitingForHoming()) state = "Home";
  else if (Job::active() && Motion::state() == M_IDLE) state = "Run";
  char b[240];
  int n = snprintf(b, sizeof(b), "<%s|MPos:%.3f,%.3f,%.3f", state, x, y, z);
  if (Job::active())
    n += snprintf(b + n, sizeof(b) - n, "|Job:%s,%.1f|Step:%d,%d,%d", Job::name(), Job::percent(), Job::run(), Job::step(), Job::nail());
  if (Motion::state() == M_ALARM) n += snprintf(b + n, sizeof(b) - n, "|A:%d", Motion::alarmCode());
  if (Motion::state() == M_PAUSE) n += snprintf(b + n, sizeof(b) - n, "|Msg:%s", Motion::pauseMessage());
  bool lx = Motion::limitActive('X'), ly = Motion::limitActive('Y'), lz = S.zMode == 1 && Motion::limitActive('Z');
  if (lx || ly || lz) n += snprintf(b + n, sizeof(b) - n, "|Pn:%s%s%s", lx ? "X" : "", ly ? "Y" : "", lz ? "Z" : "");
  n += snprintf(b + n, sizeof(b) - n, "|H:%d>", Motion::homed() ? 1 : 0);
  reply(c, b);
}

void help(Chan& c) {
  static const char* lines[] = {
    "[HLP:? status  ! hold  ~ start/resume  Ctrl-X stop]",
    "[HLP:$$ settings  $name=value  $I info  $H home  $X unlock  $J=G91 X10 F1000 jog]",
    "[HLP:$FILES  $UPLOAD=name ... $END  $DEL=name  $RUN=name  $STOP  $RESUME  $PROG  $CLEARPROG]",
    "[HLP:$MOTORS=0|1  $RST=$ factory reset  $RST=WIFI network reset  $REBOOT]",
  };
  for (auto* l : lines) reply(c, l);
}

// Returns false when the line must be retried later (planner full).
bool dollar(Chan& c, char* cmd) {
  String err;
  if (strcmp(cmd, "$") == 0) { help(c); ok(c); return true; }
  if (strcmp(cmd, "$$") == 0) { settingsPrint(*c.s); ok(c); return true; }
  if (strcmp(cmd, "$I") == 0) {
    replyf(c, "[VER:%s %s]", FW_NAME, FW_VERSION);
    replyf(c, "[OPT:z_mode=%d,y_dual=%d,homing=%d,planner=%d]", S.zMode, S.yDual, S.homingEnable, PLANNER_SIZE);
    replyf(c, "[NET:ip=%s,bt=%s]", Comms::ipText().c_str(), S.btName);
    ok(c); return true;
  }
  if (strcmp(cmd, "$H") == 0) {
    if (Job::active()) { fail(c, "job running"); return true; }
    if (!Motion::empty()) return false;
    Motion::homeAll(); ok(c); return true;
  }
  if (strcmp(cmd, "$X") == 0) {
    if (Motion::clearAlarm()) { reply(c, "[MSG:Unlocked - position not homed]"); ok(c); }
    else fail(c, "still moving");
    return true;
  }
  if (strncmp(cmd, "$J=", 3) == 0) {
    ExecResult r = gcodeJog(cmd + 3, err);
    if (r == EX_BUSY) return false;
    if (r == EX_OK) ok(c); else fail(c, err);
    return true;
  }
  if (strcmp(cmd, "$FILES") == 0) {
    File dir = LittleFS.open(JOB_DIR);
    if (dir) {
      for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (!f.isDirectory()) replyf(c, "[FILE:%s,%u]", f.name(), (unsigned)f.size());
        f.close();
      }
      dir.close();
    }
    replyf(c, "[FS:%u,%u]", (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
    ok(c); return true;
  }
  if (strncmp(cmd, "$DEL=", 5) == 0) {
    const char* n = cmd + 5;
    if (!validName(n)) { fail(c, "bad file name"); return true; }
    if (Job::isJobFile(n)) { fail(c, "file is in use"); return true; }
    if (!LittleFS.remove(jobPath(n))) { fail(c, "not found"); return true; }
    if (strcmp(Job::savedFile(), n) == 0) Job::clearProgress();
    ok(c); return true;
  }
  if (strncmp(cmd, "$UPLOAD=", 8) == 0) {
    const char* n = cmd + 8;
    if (!validName(n)) { fail(c, "bad file name (A-Z a-z 0-9 . _ -, max 31)"); return true; }
    if (Job::isJobFile(n)) { fail(c, "file is in use"); return true; }
    c.up = LittleFS.open(jobPath(n), "w");
    if (!c.up) { fail(c, "cannot create file"); return true; }
    if (strcmp(Job::savedFile(), n) == 0) Job::clearProgress();
    c.uploading = true; c.upLines = 0; c.upBytes = 0;
    ok(c); return true;
  }
  if (strncmp(cmd, "$RUN=", 5) == 0) {
    if (Job::start(cmd + 5, err)) ok(c); else fail(c, err);
    return true;
  }
  if (strcmp(cmd, "$RESUME") == 0) {
    if (Job::resume(err)) ok(c); else fail(c, err);
    return true;
  }
  if (strcmp(cmd, "$STOP") == 0) { Job::stop("stop command"); ok(c); return true; }
  if (strcmp(cmd, "$PROG") == 0) {
    String p = Job::savedProgressText();
    reply(c, p.length() ? p.c_str() : "[PROG:none]");
    ok(c); return true;
  }
  if (strcmp(cmd, "$CLEARPROG") == 0) { Job::clearProgress(); ok(c); return true; }
  if (strncmp(cmd, "$MOTORS=", 8) == 0) {
    if (!Motion::empty()) { fail(c, "machine busy"); return true; }
    Motion::motorsEnable(cmd[8] == '1'); ok(c); return true;
  }
  if (strcmp(cmd, "$RST=$") == 0) { settingsReset(); reply(c, "[MSG:Defaults restored - $REBOOT]"); ok(c); return true; }
  if (strcmp(cmd, "$RST=WIFI") == 0) { settingsResetWifi(); reply(c, "[MSG:Wi-Fi settings reset (open hotspot) - $REBOOT]"); ok(c); return true; }
  if (strcmp(cmd, "$REBOOT") == 0) { ok(c); c.s->flush(); delay(200); ESP.restart(); return true; }

  char* eq = strchr(cmd, '=');
  if (eq) {
    *eq = 0;
    if (Job::active()) { fail(c, "cannot change settings while a job runs"); return true; }
    bool reboot = false;
    String e = settingsSet(cmd + 1, eq + 1, reboot);
    if (e.length()) { fail(c, e); return true; }
    if (reboot) reply(c, "[MSG:Saved - takes effect after $REBOOT]");
    ok(c); return true;
  }
  fail(c, "unknown command, send $ for help");
  return true;
}

// Returns false when the line must be retried later.
bool handleLine(Chan& c) {
  if (c.overflow) { c.overflow = false; fail(c, "line too long"); return true; }

  if (c.uploading) {
    if (strcmp(c.buf, "$END") == 0) {
      c.up.close();
      c.uploading = false;
      replyf(c, "ok lines=%lu bytes=%lu", (unsigned long)c.upLines, (unsigned long)c.upBytes);
      return true;
    }
    if (strcmp(c.buf, "$ABORT") == 0) {
      String n = String(c.up.path());
      c.up.close();
      LittleFS.remove(n);
      c.uploading = false;
      ok(c); return true;
    }
    size_t n = strlen(c.buf);
    if (c.up.write((const uint8_t*)c.buf, n) != n || c.up.write('\n') != 1) {
      String p = String(c.up.path());
      c.up.close();
      LittleFS.remove(p);
      c.uploading = false;
      fail(c, "write failed (flash full?) - upload cancelled");
      return true;
    }
    c.upLines++; c.upBytes += n + 1;
    ok(c);
    return true;
  }

  char* l = c.buf;
  while (*l == ' ' || *l == '\t') l++;
  if (*l == 0) return true;
  if (*l == '$') return dollar(c, l);

  if (Job::active()) { fail(c, "job running"); return true; }
  String err;
  ExecResult r = gcodeExecute(l, false, 0, err);
  if (r == EX_BUSY) return false;
  if (r == EX_OK) ok(c); else fail(c, err);
  return true;
}

void realtime(Chan& c, char ch) {
  switch (ch) {
    case '?': sendStatus(c); break;
    case '!': Motion::feedHold(); break;
    case '~': Motion::cycleStart(); break;
    case 0x18:
      if (Job::active() || Job::waitingForHoming()) Job::stop("reset");
      else Motion::abort();
      reply(c, "[MSG:Stopped]");
      break;
  }
}

void pollChan(int i) {
  Chan& c = chans[i];
  if (c.pending) {
    if (!handleLine(c)) return;
    c.pending = false;
    c.len = 0;
  }
  for (int k = 0; k < 512 && c.s->available(); k++) {
    int b = c.s->read();
    if (b < 0) break;
    char ch = (char)b;
    if (!c.uploading && (ch == '?' || ch == '!' || ch == '~' || ch == 0x18)) { realtime(c, ch); continue; }
    if (ch == '\n' || ch == '\r') {
      if (c.len == 0 && !c.overflow) continue;
      c.buf[c.len] = 0;
      if (!handleLine(c)) { c.pending = true; return; }
      c.len = 0;
      continue;
    }
    if (c.len < CMD_LINE_MAX - 1) c.buf[c.len++] = ch;
    else c.overflow = true;
  }
}

void resetChan(Chan& c) {
  if (c.uploading) { String p = String(c.up.path()); c.up.close(); LittleFS.remove(p); }
  c.uploading = false; c.pending = false; c.len = 0; c.overflow = false;
}

wifi_power_t txPower(int dbm) {
  if (dbm >= 20) return WIFI_POWER_19_5dBm;
  if (dbm >= 19) return WIFI_POWER_19dBm;
  if (dbm >= 18) return WIFI_POWER_18_5dBm;
  if (dbm >= 17) return WIFI_POWER_17dBm;
  if (dbm >= 15) return WIFI_POWER_15dBm;
  if (dbm >= 13) return WIFI_POWER_13dBm;
  if (dbm >= 11) return WIFI_POWER_11dBm;
  if (dbm >= 8) return WIFI_POWER_8_5dBm;
  if (dbm >= 7) return WIFI_POWER_7dBm;
  if (dbm >= 5) return WIFI_POWER_5dBm;
  return WIFI_POWER_2dBm;
}

}  // namespace

namespace Comms {

void begin(bool safeMode) {
  chans[0].s = &Serial;
  chans[1].s = &bt;
  chans[2].s = &tcp;

  if (safeMode) {
    // Radios off: they draw the biggest current peaks. USB serial only.
    WiFi.mode(WIFI_OFF);
    Serial.printf("\r\n[MSG:%s %s SAFE MODE - Wi-Fi, Bluetooth, servo and motors are off]\r\n", FW_NAME, FW_VERSION);
    return;
  }
  WiFi.onEvent([](arduino_event_id_t) { wifiJoins++; }, ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED);
  WiFi.onEvent([](arduino_event_id_t) { wifiLeaves++; }, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);
  WiFi.mode(WIFI_AP_STA);
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  String apName = String(FW_NAME) + "-" + mac.substring(8);
  // Hand out the plotter itself as DNS server, so every name points here and phones/laptops open the dashboard.
  IPAddress apIp(192, 168, 4, 1), mask(255, 255, 255, 0);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  WiFi.softAPConfig(apIp, apIp, mask, IPAddress((uint32_t)0), apIp);
#else
  WiFi.softAPConfig(apIp, apIp, mask);
#endif
  WiFi.softAP(apName.c_str(), strlen(S.apPass) ? S.apPass : nullptr);   // no password until the user sets one
#if ESP_ARDUINO_VERSION_MAJOR >= 3 && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 2)
  if (S.captive) WiFi.AP.enableDhcpCaptivePortal();   // DHCP option 114: Android 11+/iOS 14+ show the dashboard at once
#endif
  WiFi.setTxPower(txPower(S.wifiDbm));
  if (strlen(S.wifiSsid)) WiFi.begin(S.wifiSsid, S.wifiPass);
  server.begin();
  server.setNoDelay(true);
  if (MDNS.begin("mandala")) { MDNS.addService("http", "tcp", 80); MDNS.addService("mandala", "tcp", TCP_PORT); }

  if (S.btEnable) {
    delay(300);   // let the Wi-Fi start-up current settle before Bluetooth starts
    btUp = bt.begin(S.btName);
  } else {
    WiFi.setSleep(false);   // radio is Wi-Fi only: keep it awake for quick web answers
  }
  Serial.printf("\r\n[MSG:%s %s  Wi-Fi %s (%s)  dashboard http://%s  Bluetooth %s]\r\n", FW_NAME, FW_VERSION, apName.c_str(), strlen(S.apPass) ? "password set" : "open, no password", WiFi.softAPIP().toString().c_str(), S.btEnable ? S.btName : "off");
}

void poll() {
  if (joinsShown != wifiJoins) { joinsShown = wifiJoins; Serial.printf("[MSG:Wi-Fi: a device joined the hotspot (%d connected)]\r\n", WiFi.softAPgetStationNum()); }
  if (leavesShown != wifiLeaves) { leavesShown = wifiLeaves; Serial.println("[MSG:Wi-Fi: a device left the hotspot]"); }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  WiFiClient incoming = server.accept();
#else
  WiFiClient incoming = server.available();
#endif
  if (incoming) {
    if (tcp && tcp.connected()) { tcp.println("[MSG:Replaced by new connection]"); tcp.stop(); }
    resetChan(chans[2]);
    tcp = incoming;
    tcp.setNoDelay(true);
    tcp.printf("[MSG:%s %s ready]\r\n", FW_NAME, FW_VERSION);
  }
  static bool btWas = false;
  bool btNow = connected(1);
  if (btWas && !btNow) resetChan(chans[1]);
  btWas = btNow;

  for (int i = 0; i < 3; i++) if (connected(i)) pollChan(i);
}

void broadcast(const char* line) {
  pushEvent(line);
  for (int i = 0; i < 3; i++) {
    if (!chans[i].s || !connected(i)) continue;
    chans[i].s->print(line);
    chans[i].s->print("\r\n");
  }
}

void broadcastf(const char* fmt, ...) {
  char b[192];
  va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
  if (!chans[0].s) { Serial.println(b); return; }   // before begin()
  broadcast(b);
}

bool wifiUp() { return WiFi.status() == WL_CONNECTED; }

String ipText() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  return WiFi.softAPIP().toString();
}

bool btConnected() { return connected(1); }

bool webCommand(const char* line, String& out) {
  webChan.s = &capture;
  capture.out = "";
  if (strncmp(line, "$UPLOAD", 7) == 0) { out = "error: use the dashboard upload (/api/upload)\r\n"; return true; }
  strlcpy(webChan.buf, line, sizeof(webChan.buf));
  webChan.overflow = strlen(line) >= sizeof(webChan.buf) - 1;
  bool done = handleLine(webChan);
  out = capture.out;
  capture.out = "";
  return done;
}

void webRealtime(char key, String& out) {
  webChan.s = &capture;
  capture.out = "";
  realtime(webChan, key);
  out = capture.out;
  capture.out = "";
}

String jsonEscape(const char* s) {
  String o;
  for (; *s; s++) {
    char c = *s;
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if (c == '\r') continue;
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

uint32_t eventsJson(uint32_t since, String& out) {
  out = "[";
  bool first = true;
  uint32_t from = since + 1;
  if (evSeq > (uint32_t)EV_COUNT && from < evSeq - EV_COUNT + 1) from = evSeq - EV_COUNT + 1;
  for (uint32_t s = from; s <= evSeq; s++) {
    const Ev& e = events[(s - 1) % EV_COUNT];
    if (!first) out += ",";
    first = false;
    out += '"';
    out += jsonEscape(e.text);
    out += '"';
  }
  out += "]";
  return evSeq;
}
bool tcpConnected() { return connected(2); }

}  // namespace Comms
