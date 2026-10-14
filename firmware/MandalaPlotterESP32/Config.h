// Mandala String Art XY Plotter - ESP32 firmware
// Board: ESP32 Dev Module (ESP32-WROOM-32 DevKit V1, 30 or 38 pin)
// Tools > Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"  <-- required (Wi-Fi + Bluetooth do not fit the default)
#pragma once

#define FW_NAME    "MandalaPlotter"
#define FW_VERSION "1.3.0"

// ---------------- Stepper drivers (A4988) ----------------
#define PIN_X_STEP   26
#define PIN_X_DIR    27
#define PIN_Y_STEP   25
#define PIN_Y_DIR    33
#define PIN_Y2_STEP  32   // second Y driver, only used when y_dual=1
#define PIN_Y2_DIR   14
#define PIN_Z_STEP   18   // only used when z_mode=1 (stepper)
#define PIN_Z_DIR    19
#define PIN_EN       13   // shared ENABLE of all A4988 (active LOW)

// ---------------- Z servo (z_mode=2) ----------------
#define PIN_SERVO    23

// ---------------- Limit switches (to GND, 10k pull-up to 3V3 required: input-only pins) ----------------
#define PIN_LIM_X    34
#define PIN_LIM_Y    35
#define PIN_LIM_Z    39

// ---------------- Buttons (to GND, internal pull-up) ----------------
#define PIN_BTN_START 4   // short: pause/resume   long (2 s): resume saved job
#define PIN_BTN_STOP  16  // short: stop job / clear alarm
#define PIN_BTN_HOME  17  // short: home all axes

// ---------------- OLED SSD1306 128x64 I2C ----------------
#define PIN_SDA      21
#define PIN_SCL      22
#define OLED_ADDR    0x3C

#define PIN_BOOT     0    // on-board BOOT button: hold 5 s to reset Wi-Fi settings (open hotspot)
#define PIN_LED      2    // on-board LED: on = Wi-Fi/BT client connected, blinks while running

// ---------------- Sizes ----------------
#define PLANNER_SIZE 48     // queued motion segments
#define CMD_LINE_MAX     160    // longest command / G-code line
#define TCP_PORT     23     // raw TCP (Wi-Fi) command port
#define JOB_DIR      "/jobs"
#define MSG_MAX      40
