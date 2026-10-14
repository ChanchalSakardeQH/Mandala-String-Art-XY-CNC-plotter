#pragma once
#include <Arduino.h>

// All machine settings. Changed with "$name=value", listed with "$$", stored in flash (NVS).
struct Settings {
  // steps per mm (A4988 at 1/16 microstep, GT2 belt, 20T pulley = 80)
  float xSteps, ySteps, zSteps;
  // travel limits in mm (origin = home switch)
  float xMax, yMax, zMax;
  // direction inversion (0/1)
  int xInv, yInv, y2Inv, zInv;
  int yDual;      // 0 = one Y driver (or two drivers wired in parallel), 1 = second driver on Y2 pins
  int zMode;      // 0 = no Z, 1 = Z stepper, 2 = Z servo
  int servoUp, servoDown, servoMs; // servo angles (deg) and settle time (ms)
  float zSplit;   // servo: Z above this = pen/needle up
  float zSafe;    // Z height used for travel when resuming
  float maxRateXY, maxRateZ;  // mm/min
  float accelXY, accelZ;      // mm/s^2
  float arcRate;              // mm/min cap while wrapping (arcs)
  float arcTol;               // mm, arc chord tolerance
  int homingEnable;
  float homingSeek, homingFeed, homingPulloff; // mm/min, mm/min, mm
  int homeDirMask;  // bit0 X, bit1 Y, bit2 Z: 1 = switch at max end
  int homeZ;        // home Z too (Z stepper only)
  int limitInvert;  // 0 = switch closes to GND (active LOW)
  int hardLimits, softLimits;
  int enableInvert; // 0 = A4988 (EN active LOW)
  int wifiDbm;     // Wi-Fi transmit power; lower = smaller current peaks on weak USB power
  int btEnable;    // 1 = classic Bluetooth on. Off by default: Bluetooth + Wi-Fi hotspot is unstable on ESP32 (Espressif rates it "C1")
  int captive;     // 1 = joining the hotspot opens the dashboard automatically (captive portal)
  char wifiSsid[33], wifiPass[65], apPass[65], btName[33];
  char uiTheme[17]; // dashboard theme, so every phone/PC opens with the same look
};

extern Settings S;

void settingsLoad();
// Returns "" on success, otherwise an error text. Sets needReboot if the change applies after restart.
String settingsSet(const char* name, const char* value, bool& needReboot);
void settingsPrint(Print& out);
void settingsReset();
void settingsResetWifi();
