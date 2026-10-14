// Web dashboard on port 80: serves the dashboard page (embedded, gzipped) and a small HTTP API.
//   GET  /                     dashboard
//   GET  /api/status?since=N   machine state + events newer than N (JSON)
//   POST /api/cmd              body = one command line ($H, $$, G-code, ...) -> reply lines (text); 503 = busy, retry
//   POST /api/rt?k=hold|resume|stop|status
//   POST /api/upload?name=F    multipart file upload into /jobs/F
// Captive portal: on the hotspot every DNS name points to the plotter, so phones and laptops
// show the dashboard by themselves after joining (Windows opens the browser, Android/iOS show a sign-in page).
// All answers carry "Access-Control-Allow-Origin: *" so the offline web page can also talk to the plotter.
#include "Web.h"
#include "Config.h"
#include "Settings.h"
#include "Motion.h"
#include "Job.h"
#include "Comms.h"
#include "WebPage.h"
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <LittleFS.h>

namespace {

WebServer http(80);
DNSServer dns;
bool dnsUp = false;

File upFile;
String upError;
uint32_t upBytes = 0;
char upName[32] = "";

bool validName(const String& n) {
  if (n.length() == 0 || n.length() > 31 || n[0] == '.') return false;
  for (size_t i = 0; i < n.length(); i++) {
    char c = n[i];
    if (!(isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-')) return false;
  }
  return true;
}

void cors() {
  http.sendHeader("Access-Control-Allow-Origin", "*");
  http.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  http.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
}

void sendJson(int code, const String& body) {
  cors();
  http.sendHeader("Cache-Control", "no-store");
  http.send(code, "application/json", body);
}

void handleRoot() {
  uint32_t t0 = millis();
  Serial.printf("[MSG:Web: page requested by %s]\r\n", http.client().remoteIP().toString().c_str());
  http.sendHeader("Content-Encoding", "gzip");
  http.sendHeader("Cache-Control", "no-cache");
  http.send_P(200, "text/html; charset=utf-8", (const char*)WEB_PAGE_GZ, WEB_PAGE_GZ_LEN);
  Serial.printf("[MSG:Web: page sent, %u bytes in %lu ms]\r\n", (unsigned)WEB_PAGE_GZ_LEN, (unsigned long)(millis() - t0));
}

void handleStatus() {
  uint32_t since = http.hasArg("since") ? (uint32_t)strtoul(http.arg("since").c_str(), nullptr, 10) : 0;
  float x, y, z;
  Motion::actual(x, y, z);
  const char* state = Motion::stateName();
  if (Job::waitingForHoming()) state = "Home";
  else if (Job::active() && Motion::state() == M_IDLE) state = "Run";
  String ev;
  uint32_t seq = Comms::eventsJson(since, ev);
  char buf[200];
  String j = "{";
  snprintf(buf, sizeof(buf), "\"ver\":\"%s\",\"state\":\"%s\",\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"homed\":%s,\"alarm\":%d,",
           FW_VERSION, state, x, y, z, Motion::homed() ? "true" : "false",
           Motion::state() == M_ALARM ? Motion::alarmCode() : 0);
  j += buf;
  j += "\"msg\":\"" + Comms::jsonEscape(Motion::pauseMessage()) + "\",";
  if (Job::active()) {
    snprintf(buf, sizeof(buf), "\"job\":{\"active\":true,\"pct\":%.1f,\"run\":%d,\"step\":%d,\"nail\":%d,\"name\":\"",
             Job::percent(), Job::run(), Job::step(), Job::nail());
    j += buf;
    j += Comms::jsonEscape(Job::name());
    j += "\"},";
  } else {
    j += "\"job\":{\"active\":false},";
  }
  j += "\"saved\":\"" + Comms::jsonEscape(Job::savedProgressText().c_str()) + "\",";
  String pins = "";
  if (Motion::limitActive('X')) pins += 'X';
  if (Motion::limitActive('Y')) pins += 'Y';
  if (S.zMode == 1 && Motion::limitActive('Z')) pins += 'Z';
  j += "\"pins\":\"" + pins + "\",";
  j += "\"theme\":\"" + Comms::jsonEscape(S.uiTheme) + "\",";
  j += S.homingEnable ? "\"homing\":1," : "\"homing\":0,";
  snprintf(buf, sizeof(buf), "\"ip\":\"%s\",\"bt\":%s,\"tcp\":%s,\"zMode\":%d,\"xMax\":%.1f,\"yMax\":%.1f,\"seq\":%lu,",
           Comms::ipText().c_str(), Comms::btConnected() ? "true" : "false", Comms::tcpConnected() ? "true" : "false",
           S.zMode, S.xMax, S.yMax, (unsigned long)seq);
  j += buf;
  j += "\"events\":" + ev + "}";
  sendJson(200, j);
}

void handleCmd() {
  String line = http.arg("plain");
  line.trim();
  if (line.length() == 0) { cors(); http.send(400, "text/plain", "error: empty command\r\n"); return; }
  if (line.indexOf('\n') >= 0) { cors(); http.send(400, "text/plain", "error: one command per request\r\n"); return; }
  String out;
  bool done = Comms::webCommand(line.c_str(), out);
  cors();
  http.sendHeader("Cache-Control", "no-store");
  if (!done) { http.send(503, "text/plain", "busy\r\n"); return; }
  http.send(200, "text/plain", out);
}

void handleRealtime() {
  String k = http.arg("k");
  char key = 0;
  if (k == "status") key = '?';
  else if (k == "hold") key = '!';
  else if (k == "resume") key = '~';
  else if (k == "stop") key = 0x18;
  cors();
  if (!key) { http.send(400, "text/plain", "error: k must be hold, resume, stop or status\r\n"); return; }
  String out;
  Comms::webRealtime(key, out);
  http.send(200, "text/plain", out.length() ? out : String("ok\r\n"));
}

void handleUploadData() {
  HTTPUpload& up = http.upload();
  if (up.status == UPLOAD_FILE_START) {
    upError = "";
    upBytes = 0;
    String name = http.arg("name");
    if (name.length() == 0) name = up.filename;
    if (!validName(name)) { upError = "bad file name (A-Z a-z 0-9 . _ -, max 31)"; return; }
    if (Job::isJobFile(name.c_str())) { upError = "that file is being run"; return; }
    strlcpy(upName, name.c_str(), sizeof(upName));
    upFile = LittleFS.open(String(JOB_DIR) + "/" + name, "w");
    if (!upFile) { upError = "cannot create file"; return; }
    if (strcmp(Job::savedFile(), upName) == 0) Job::clearProgress();
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (upError.length() || !upFile) return;
    if (upFile.write(up.buf, up.currentSize) != up.currentSize) {
      upError = "write failed (flash full?)";
      upFile.close();
      LittleFS.remove(String(JOB_DIR) + "/" + upName);
      return;
    }
    upBytes += up.currentSize;
  } else if (up.status == UPLOAD_FILE_END) {
    if (upFile) upFile.close();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (upFile) { upFile.close(); LittleFS.remove(String(JOB_DIR) + "/" + upName); }
    upError = "upload aborted";
  }
}

void handleUploadDone() {
  if (upError.length()) { sendJson(400, "{\"ok\":false,\"error\":\"" + Comms::jsonEscape(upError.c_str()) + "\"}"); return; }
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"ok\":true,\"name\":\"%s\",\"bytes\":%lu}", upName, (unsigned long)upBytes);
  sendJson(200, buf);
}

// Requests for any other host name (connectivity checks such as msftconnecttest.com,
// connectivitycheck.gstatic.com or captive.apple.com) are sent to the dashboard.
bool isOurHost() {
  String h = http.hostHeader();
  int colon = h.indexOf(':');
  if (colon >= 0) h = h.substring(0, colon);
  h.toLowerCase();
  return h.length() == 0 || h == WiFi.softAPIP().toString() || h == WiFi.localIP().toString() || h == "mandala.local" || h == "mandala";
}

void redirectHome() {
  http.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  http.sendHeader("Cache-Control", "no-store");
  http.send(302, "text/plain", "");
}

void handleNotFound() {
  if (http.method() == HTTP_OPTIONS) { cors(); http.send(204); return; }
  if (dnsUp && !isOurHost()) { redirectHome(); return; }
  cors();
  http.send(404, "text/plain", "not found\r\n");
}

}  // namespace

namespace Web {

void begin() {
  http.on("/", HTTP_GET, handleRoot);
  http.on("/index.html", HTTP_GET, handleRoot);
  http.on("/api/status", HTTP_GET, handleStatus);
  http.on("/api/cmd", HTTP_POST, handleCmd);
  http.on("/api/rt", HTTP_POST, handleRealtime);
  http.on("/api/upload", HTTP_POST, handleUploadDone, handleUploadData);
  http.onNotFound(handleNotFound);
  http.begin();
  if (S.captive) {
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dnsUp = dns.start(53, "*", WiFi.softAPIP());
  }
}

void poll() {
  if (dnsUp) dns.processNextRequest();
  http.handleClient();
}

}  // namespace Web
