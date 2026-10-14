#include "Settings.h"
#include <Preferences.h>

Settings S;

enum SType : uint8_t { T_FLOAT, T_INT, T_STR };

struct SDesc {
  const char* name;   // max 15 chars (NVS key limit)
  SType type;
  void* ptr;
  float def, mn, mx;  // numbers
  const char* sdef;   // strings
  uint8_t len;        // string buffer size
  bool reboot;
};

#define F_(n, field, d, lo, hi, rb) {n, T_FLOAT, &S.field, d, lo, hi, nullptr, 0, rb}
#define I_(n, field, d, lo, hi, rb) {n, T_INT, &S.field, d, lo, hi, nullptr, 0, rb}
#define S_(n, field, d, rb)         {n, T_STR, S.field, 0, 0, 0, d, sizeof(S.field), rb}

static const SDesc TABLE[] = {
  F_("x_steps_mm", xSteps, 80, 1, 2000, false),
  F_("y_steps_mm", ySteps, 80, 1, 2000, false),
  F_("z_steps_mm", zSteps, 400, 1, 5000, false),
  F_("x_max", xMax, 400, 10, 2000, false),
  F_("y_max", yMax, 400, 10, 2000, false),
  F_("z_max", zMax, 10, 1, 200, false),
  I_("x_dir_inv", xInv, 0, 0, 1, true),
  I_("y_dir_inv", yInv, 0, 0, 1, true),
  I_("y2_dir_inv", y2Inv, 1, 0, 1, true),
  I_("z_dir_inv", zInv, 0, 0, 1, true),
  I_("y_dual", yDual, 0, 0, 1, true),
  I_("z_mode", zMode, 2, 0, 2, true),
  I_("servo_up", servoUp, 90, 0, 180, false),
  I_("servo_down", servoDown, 30, 0, 180, false),
  I_("servo_ms", servoMs, 250, 0, 3000, false),
  F_("z_split", zSplit, 0.5, -100, 100, false),
  F_("z_safe", zSafe, 5, -100, 200, false),
  F_("max_rate_xy", maxRateXY, 6000, 10, 60000, false),
  F_("max_rate_z", maxRateZ, 600, 1, 10000, false),
  F_("accel_xy", accelXY, 1500, 10, 20000, false),
  F_("accel_z", accelZ, 300, 1, 10000, false),
  F_("arc_rate", arcRate, 1200, 10, 60000, false),
  F_("arc_tol", arcTol, 0.01, 0.001, 0.5, false),
  I_("homing_enable", homingEnable, 0, 0, 1, false),   // off until limit switches are fitted (Machine setup)
  F_("homing_seek", homingSeek, 1500, 10, 10000, false),
  F_("homing_feed", homingFeed, 150, 5, 2000, false),
  F_("homing_pulloff", homingPulloff, 2, 0.2, 20, false),
  I_("home_dir_mask", homeDirMask, 0, 0, 7, false),
  I_("home_z", homeZ, 0, 0, 1, false),
  I_("limit_invert", limitInvert, 0, 0, 1, false),
  I_("hard_limits", hardLimits, 0, 0, 1, false),
  I_("soft_limits", softLimits, 1, 0, 1, false),
  I_("enable_invert", enableInvert, 0, 0, 1, true),
  I_("wifi_dbm", wifiDbm, 11, 2, 20, true),
  I_("bt_enable", btEnable, 0, 0, 1, true),
  S_("wifi_ssid", wifiSsid, "", true),
  S_("wifi_pass", wifiPass, "", true),
  S_("ap_pass", apPass, "", true),        // empty = open hotspot (set one later in Machine setup)
  S_("bt_name", btName, "MandalaPlotter", true),
  I_("captive", captive, 1, 0, 1, true),
  S_("ui_theme", uiTheme, "light", false),
};
static const size_t COUNT = sizeof(TABLE) / sizeof(TABLE[0]);

static Preferences prefs;

static void applyDefault(const SDesc& d) {
  switch (d.type) {
    case T_FLOAT: *(float*)d.ptr = d.def; break;
    case T_INT: *(int*)d.ptr = (int)d.def; break;
    case T_STR: strlcpy((char*)d.ptr, d.sdef, d.len); break;
  }
}

void settingsLoad() {
  prefs.begin("cfg", true);
  for (size_t i = 0; i < COUNT; i++) {
    const SDesc& d = TABLE[i];
    applyDefault(d);
    if (!prefs.isKey(d.name)) continue;
    switch (d.type) {
      case T_FLOAT: *(float*)d.ptr = prefs.getFloat(d.name, d.def); break;
      case T_INT: *(int*)d.ptr = prefs.getInt(d.name, (int)d.def); break;
      case T_STR: prefs.getString(d.name, (char*)d.ptr, d.len); break;
    }
  }
  prefs.end();
}

// Forgotten password? Clears only the network settings (hotspot password, home Wi-Fi, Bluetooth,
// Wi-Fi power, auto-open) so the plotter comes back as an open hotspot. Machine settings stay.
void settingsResetWifi() {
  static const char* keys[] = {"ap_pass", "wifi_ssid", "wifi_pass", "bt_enable", "bt_name", "wifi_dbm", "captive"};
  prefs.begin("cfg", false);
  for (auto* k : keys) prefs.remove(k);
  prefs.end();
  settingsLoad();
}

void settingsReset() {
  prefs.begin("cfg", false);
  prefs.clear();
  prefs.end();
  settingsLoad();
}

String settingsSet(const char* name, const char* value, bool& needReboot) {
  for (size_t i = 0; i < COUNT; i++) {
    const SDesc& d = TABLE[i];
    if (strcmp(d.name, name) != 0) continue;
    prefs.begin("cfg", false);
    String err = "";
    if (d.type == T_STR) {
      if (strlen(value) >= d.len) err = "value too long";
      else if (d.ptr == S.apPass && strlen(value) > 0 && strlen(value) < 8) err = "ap_pass: leave empty for no password, or use at least 8 characters";
      else { strlcpy((char*)d.ptr, value, d.len); prefs.putString(d.name, value); }
    } else {
      char* end;
      float v = strtof(value, &end);
      if (end == value || *end != 0) err = "not a number";
      else if (v < d.mn || v > d.mx) err = "out of range " + String(d.mn, 3) + ".." + String(d.mx, 3);
      else if (d.type == T_FLOAT) { *(float*)d.ptr = v; prefs.putFloat(d.name, v); }
      else { *(int*)d.ptr = (int)v; prefs.putInt(d.name, (int)v); }
    }
    prefs.end();
    if (err.length() == 0) needReboot = d.reboot;
    return err;
  }
  return "unknown setting";
}

void settingsPrint(Print& out) {
  for (size_t i = 0; i < COUNT; i++) {
    const SDesc& d = TABLE[i];
    out.print('$'); out.print(d.name); out.print('=');
    switch (d.type) {
      case T_FLOAT: out.print(*(float*)d.ptr, 3); break;
      case T_INT: out.print(*(int*)d.ptr); break;
      case T_STR:
        // never echo passwords
        if (strstr(d.name, "pass")) out.print(strlen((char*)d.ptr) ? "********" : "");
        else out.print((char*)d.ptr);
        break;
    }
    out.print("\r\n");
  }
}
