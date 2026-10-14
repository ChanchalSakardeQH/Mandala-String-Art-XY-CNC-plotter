#pragma once
#include <Arduino.h>
#include "Config.h"

enum MachState : uint8_t { M_IDLE, M_RUN, M_HOLD, M_PAUSE, M_HOMING, M_ALARM };

enum SegType : uint8_t {
  SEG_MOVE,    // straight move to x,y,z
  SEG_DWELL,   // wait dwellMs
  SEG_MARK,    // "; STEP" progress marker from a job file
  SEG_PAUSE,   // M0: wait for Start / '~'
  SEG_END,     // M2: job finished
  SEG_MOTORS,  // M17/M18: dwellMs = 1 enable, 0 disable
};

#define SEGF_RAPID 0x01
#define SEGF_ARC   0x02
#define SEGF_JOG   0x04

struct Segment {
  SegType type;
  uint8_t flags;
  float x, y, z;      // mm target (SEG_MOVE)
  float feed;         // mm/min
  uint32_t dwellMs;
  int16_t run, step, nail;
  uint32_t offset;    // file offset of the marker line
  char msg[MSG_MAX];
};

namespace Motion {
  void begin(bool safeMode);
  void service();

  // Planner
  bool canQueue(int n = 1);
  bool queue(const Segment& s);
  bool queueMove(float x, float y, float z, float feed, uint8_t flags);
  bool arcBusy();
  void startArc(float cx, float cy, float r, float a0, float sweep, float ex, float ey, float ez, float feed);
  bool empty();       // nothing queued and nothing moving
  bool onlyJogging(); // everything queued/moving is a jog

  // Planned position = where the last queued move ends (used by the G-code parser)
  float plannedX(); float plannedY(); float plannedZ();

  // Actual position from the steppers
  void actual(float& x, float& y, float& z);

  // Control
  void feedHold();
  void cycleStart();
  void abort();       // decelerate, flush everything (no alarm)
  void homeAll();
  void alarm(int code, const char* msg);
  bool clearAlarm();
  void motorsEnable(bool on);

  MachState state();
  const char* stateName();
  bool homed();
  bool homingRequired();
  const char* pauseMessage();
  bool limitActive(char axis);
  int alarmCode();
}

// Implemented in Job.cpp, called when the executor reaches these segments.
void jobOnMarker(const Segment& s);
void jobOnPause(const Segment& s);
void jobOnEnd();
