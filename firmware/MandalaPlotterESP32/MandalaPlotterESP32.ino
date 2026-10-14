// =====================================================================
//  Mandala String Art XY Plotter - ESP32 firmware  (woodyouloveit)
//  Marks nail positions and winds thread from G-code jobs stored in flash.
//
//  Board  : ESP32 Dev Module (ESP32-WROOM-32 DevKit V1)
//  Partition Scheme : Huge APP (3MB No OTA/1MB SPIFFS)   <-- required
//  ESP32 core: 2.0.17 or 3.x (both compile-tested)
//  Libraries: FastAccelStepper >= 2.0.0, Adafruit SSD1306, Adafruit GFX
//  Dashboard: http://192.168.4.1 (hotspot) or http://mandala.local  (port 80)
//  Talks to the Android app over Bluetooth (classic) and Wi-Fi (TCP port 23),
//  and to a PC over USB at 115200 baud. Protocol: see ../../docs/PROTOCOL.md
// =====================================================================
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_system.h>
#include "Config.h"
#include "Settings.h"
#include "Motion.h"
#include "Job.h"
#include "Comms.h"
#include "Ui.h"
#include "Web.h"

// ---- boot-loop guard ----
// Every boot that ends within 15 s counts as "quick". After 3 quick boots in a row the plotter
// starts in SAFE MODE (no Wi-Fi, Bluetooth, servo or motors) so a power problem can't keep it
// rebooting, and it prints why the last reset happened.
static bool safeMode = false;
static bool bootCleared = false;

static const char* resetReason(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_BROWNOUT: return "BROWNOUT - the supply voltage dropped (weak USB port/cable, or servo/motors/OLED powered from the board)";
    case ESP_RST_PANIC: return "CRASH (software panic) - please send the log";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "WATCHDOG - the program stopped responding - please send the log";
    case ESP_RST_SW: return "restart command";
    case ESP_RST_EXT: return "reset button";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default: return "unknown";
  }
}

void setup() {
  Serial.begin(115200);
  esp_reset_reason_t why = esp_reset_reason();
  Preferences boot;
  boot.begin("boot", false);
  int quick = boot.getInt("quick", 0) + 1;
  boot.putInt("quick", quick);
  boot.end();
  safeMode = quick >= 3;
  Serial.printf("\r\n[MSG:Last reset: %s]\r\n", resetReason(why));
  if (safeMode) Serial.printf("[MSG:%d resets in a row - starting in SAFE MODE]\r\n", quick);

  settingsLoad();
  if (!LittleFS.begin(true)) Serial.println("[MSG:LittleFS mount failed]");
  if (!LittleFS.exists(JOB_DIR)) LittleFS.mkdir(JOB_DIR);
  Comms::begin(safeMode);
  if (!safeMode) Web::begin();
  Motion::begin(safeMode);
  Job::begin();
  Ui::begin();
  if (safeMode) {
    Comms::broadcast("ALARM:12 Safe mode after repeated resets. Check the power wiring (see docs/WIRING.md), then send $REBOOT");
  } else if (Motion::state() == M_ALARM) {
    Comms::broadcast("ALARM:11 Home the machine first: press Home (limit switches are switched on in Machine setup)");
  } else {
    Comms::broadcast("[MSG:Ready]");
  }
}

void loop() {
  if (!bootCleared && millis() > 15000) {   // ran long enough: this boot was not part of a loop
    Preferences boot;
    boot.begin("boot", false);
    boot.putInt("quick", 0);
    boot.end();
    bootCleared = true;
  }
  Comms::poll();
  if (!safeMode) Web::poll();
  Ui::poll();
  Job::service();
  Motion::service();
}
