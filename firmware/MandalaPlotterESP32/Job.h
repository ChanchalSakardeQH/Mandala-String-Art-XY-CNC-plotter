#pragma once
#include <Arduino.h>

namespace Job {
  void begin();
  void service();

  bool start(const char* name, String& err);   // run a stored file from the beginning
  bool resume(String& err);                    // continue the last stopped job from its saved step
  void stop(const char* why);                  // stop running job, keep progress for resume

  bool active();
  bool waitingForHoming();
  const char* name();
  float percent();
  int run();
  int step();
  int nail();
  bool hasSavedProgress();
  String savedProgressText();   // "[PROG:file,run,step,nail]" or ""
  void clearProgress();
  bool isJobFile(const char* name);  // true if the running job uses this file
  const char* savedFile();           // file of the saved progress, "" if none
}
