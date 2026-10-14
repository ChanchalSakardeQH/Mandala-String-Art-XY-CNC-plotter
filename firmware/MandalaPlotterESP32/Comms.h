#pragma once
#include <Arduino.h>

namespace Comms {
  void begin(bool safeMode);
  void poll();
  void broadcast(const char* line);
  void broadcastf(const char* fmt, ...);
  bool wifiUp();          // joined the home Wi-Fi (the hotspot is always on)
  String ipText();        // "192.168.4.1" / station IP
  bool btConnected();
  bool tcpConnected();

  // ---- used by the web dashboard (Web.cpp) ----
  // Runs one command line; the plotter's reply lines are returned in out.
  // Returns false (and runs nothing) when the planner is full: try again shortly.
  bool webCommand(const char* line, String& out);
  void webRealtime(char key, String& out);           // '?', '!', '~' or 0x18
  uint32_t eventsJson(uint32_t since, String& out);   // broadcast messages after "since" as a JSON array
  String jsonEscape(const char* s);
}
