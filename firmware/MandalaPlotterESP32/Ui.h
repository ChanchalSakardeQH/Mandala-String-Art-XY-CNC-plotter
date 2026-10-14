#pragma once
#include <Arduino.h>

namespace Ui {
  void begin();
  void poll();    // buttons + LED, call every loop
  void update();  // refresh the OLED snapshot
}
