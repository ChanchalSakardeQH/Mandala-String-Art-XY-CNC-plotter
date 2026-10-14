#include "Ui.h"
#include "Config.h"
#include "Settings.h"
#include "Motion.h"
#include "Job.h"
#include "Comms.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

namespace {

// Snapshot shared with the display task (runs on core 0, so I2C never delays motion on core 1).
struct Snap {
  char state[8];
  float x, y, z;
  bool job;
  char name[32];
  float pct;
  int run, step, nail;
  char msg[MSG_MAX];
  char ip[16];
  bool bt, tcp, sta, needsHome, saved;
  int alarm;
};
Snap snap;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
bool oledOk = false;

void displayTask(void*) {
  Snap s;
  for (;;) {
    portENTER_CRITICAL(&mux);
    s = snap;
    portEXIT_CRITICAL(&mux);

    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.printf("MANDALA %s%s%s", s.sta ? "WiFi " : "AP ", s.bt ? "BT " : "", s.tcp ? "TCP" : "");

    oled.setTextSize(2);
    oled.setCursor(0, 11);
    if (s.job && s.state[0] == 'R') oled.printf("RUN %3.0f%%", s.pct);
    else oled.print(s.state);
    if (s.needsHome && s.state[0] != 'H') { oled.setTextSize(1); oled.setCursor(104, 11); oled.print("!HM"); }

    oled.setTextSize(1);
    oled.setCursor(0, 30);
    oled.printf("X%.1f Y%.1f Z%.1f", s.x, s.y, s.z);
    oled.setCursor(0, 40);
    if (s.alarm == 11) oled.print("Press Home to start");
    else if (s.alarm) oled.printf("Alarm %d - see dashboard", s.alarm);
    else if (s.msg[0]) { oled.print(s.msg); }   // wraps over two lines
    else if (s.job) oled.printf("Run %d Step %d N%d", s.run, s.step, s.nail);
    else if (s.saved) oled.print("Saved job: hold Start");
    else oled.print("Ready");
    oled.setCursor(0, 56);
    oled.print(s.ip);
    oled.display();
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

struct Button {
  int pin;
  bool down;
  uint32_t since;
  bool longFired;
  // Explicit constructor: ESP32 core 2.x compiles as C++11, where a struct with
  // default member values cannot be brace-initialised like Button{PIN}.
  explicit Button(int p) : pin(p), down(false), since(0), longFired(false) {}
};
Button bStart{PIN_BTN_START}, bStop{PIN_BTN_STOP}, bHome{PIN_BTN_HOME};
uint32_t bootSince = 0;     // BOOT button held since (0 = not held)
char notice[MSG_MAX] = "";  // short message that overrides the OLED message line

// returns 1 = short press (on release), 2 = long press (after 2 s, once)
int pollButton(Button& b) {
  bool pressed = digitalRead(b.pin) == LOW;
  uint32_t now = millis();
  if (pressed && !b.down) { b.down = true; b.since = now; b.longFired = false; return 0; }
  if (pressed && b.down && !b.longFired && now - b.since > 2000) { b.longFired = true; return 2; }
  if (!pressed && b.down) {
    b.down = false;
    if (!b.longFired && now - b.since > 30) return 1;   // 30 ms debounce
  }
  return 0;
}

}  // namespace

namespace Ui {

void begin() {
  pinMode(PIN_BTN_START, INPUT_PULLUP);
  pinMode(PIN_BTN_STOP, INPUT_PULLUP);
  pinMode(PIN_BTN_HOME, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  oledOk = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  memset(&snap, 0, sizeof(snap));
  update();
  if (oledOk) xTaskCreatePinnedToCore(displayTask, "oled", 4096, nullptr, 1, nullptr, 0);
  else Serial.println("[MSG:OLED not found - continuing without display]");
}

void poll() {
  int s = pollButton(bStart);
  MachState m = Motion::state();
  if (s == 1) {
    if (m == M_HOLD || m == M_PAUSE) Motion::cycleStart();
    else if (m == M_RUN) Motion::feedHold();
  } else if (s == 2 && !Job::active() && Motion::empty()) {
    String err;
    if (!Job::resume(err)) Comms::broadcastf("[MSG:Resume: %s]", err.c_str());
  }
  if (pollButton(bStop) == 1) {
    if (Job::active() || Job::waitingForHoming()) Job::stop("Stop button");
    else if (!Motion::empty()) Motion::abort();
  }
  if (pollButton(bHome) == 1 && !Job::active() && (Motion::empty() || m == M_ALARM)) Motion::homeAll();

  // BOOT button held 5 s: forget hotspot password / home Wi-Fi and restart as an open hotspot
  uint32_t t = millis();
  if (digitalRead(PIN_BOOT) == LOW && !Job::active()) {
    if (!bootSince) bootSince = t;
    uint32_t held = t - bootSince;
    if (held > 800) snprintf(notice, sizeof(notice), "Wi-Fi reset in %lus", (unsigned long)((5000 - min(held, (uint32_t)5000)) / 1000 + 1));
    if (held >= 5000) {
      settingsResetWifi();
      strlcpy(notice, "Wi-Fi reset - restarting", sizeof(notice));
      update();
      Serial.println("[MSG:Wi-Fi settings reset by BOOT button - restarting as open hotspot]");
      delay(1200);
      ESP.restart();
    }
  } else if (bootSince) {
    bootSince = 0;
    notice[0] = 0;
  }

  // LED: steady = app connected, slow blink = running, fast blink = alarm
  bool led;
  if (m == M_ALARM) led = (t / 120) % 2;
  else if (m == M_RUN || Job::active()) led = (t / 500) % 2;
  else led = Comms::btConnected() || Comms::tcpConnected();
  digitalWrite(PIN_LED, led);

  static uint32_t last = 0;
  if (t - last >= 200) { last = t; update(); }
}

void update() {
  Snap s{};
  const char* st = Motion::stateName();
  if (Job::waitingForHoming()) st = "Home";
  else if (Job::active() && Motion::state() == M_IDLE) st = "Run";
  strlcpy(s.state, st, sizeof(s.state));
  Motion::actual(s.x, s.y, s.z);
  s.job = Job::active();
  strlcpy(s.name, Job::name(), sizeof(s.name));
  s.pct = Job::percent();
  s.run = Job::run(); s.step = Job::step(); s.nail = Job::nail();
  strlcpy(s.msg, notice[0] ? notice : Motion::pauseMessage(), sizeof(s.msg));
  strlcpy(s.ip, Comms::ipText().c_str(), sizeof(s.ip));
  s.bt = Comms::btConnected();
  s.tcp = Comms::tcpConnected();
  s.sta = Comms::wifiUp();
  s.needsHome = Motion::homingRequired();
  s.saved = Job::hasSavedProgress();
  s.alarm = Motion::state() == M_ALARM ? Motion::alarmCode() : 0;
  portENTER_CRITICAL(&mux);
  snap = s;
  portEXIT_CRITICAL(&mux);
}

}  // namespace Ui
