# Wiring

See `wiring-diagram.svg` (or `.png` to share on WhatsApp). The diagram groups ESP32 pins by function, so check the printed label on your board for each pin's physical position.

## Pin table

| ESP32 pin | Connects to | Notes |
|---|---|---|
| GPIO26 | A4988 X — STEP | |
| GPIO27 | A4988 X — DIR | |
| GPIO25 | A4988 Y — STEP | |
| GPIO33 | A4988 Y — DIR | |
| GPIO32 | A4988 Y2 — STEP | only with `y_dual=1` |
| GPIO14 | A4988 Y2 — DIR | only with `y_dual=1` |
| GPIO18 | A4988 Z — STEP | only with `z_mode=1` |
| GPIO19 | A4988 Z — DIR | only with `z_mode=1` |
| GPIO13 | ENABLE of **all** A4988 | one wire to every driver |
| GPIO23 | Servo signal (orange) | only with `z_mode=2` |
| GPIO34 | X limit switch | **10k pull-up to 3V3 + 100nF to GND** |
| GPIO35 | Y limit switch | same |
| GPIO39 (VN) | Z limit switch | same; Z stepper only |
| GPIO4 | START button → GND | internal pull-up |
| GPIO16 | STOP button → GND | internal pull-up |
| GPIO17 | HOME button → GND | internal pull-up |
| GPIO21 | OLED SDA | |
| GPIO22 | OLED SCL | |
| GPIO2 | on-board LED | nothing to wire |
| 3V3 | OLED VCC, A4988 VDD, limit pull-ups | |
| VIN (5V) | buck converter 5 V out | some boards label this pin 5V |
| GND | common ground | |

**Each A4988 module:**
- MS1, MS2, MS3 → VDD (1/16 microstepping).
- RESET joined to SLEEP.
- VMOT → +12 V, with a 100 µF / 25 V capacitor across VMOT–GND right at the driver.
- 1A/1B → motor coil A, 2A/2B → motor coil B. To find the pairs, use a multimeter: the two wires of one coil read a few ohms between them.

**Two Y motors, two ways:**
- With `y_dual=1`: the second driver uses GPIO32/14, and `y2_dir_inv` reverses it in software.
- With `y_dual=0`: wire the second driver's STEP/DIR to GPIO25/33, the same pins as Y. Reverse that motor by swapping its 2A and 2B wires.

Never put two motors on one A4988.

## Parts list (one machine)

| Qty | Part | Notes |
|---|---|---|
| 1 | ESP32 DevKit V1 (ESP32-WROOM-32) | 30 or 38 pin; **not** S2/S3/C3 |
| 2–4 | A4988 driver module + heatsink | X, Y (+ Y2, + Z) |
| 2–4 | NEMA17 stepper (e.g. 17HS4401, 1.5–1.7 A) | |
| 1 | SSD1306 0.96" OLED, I2C, 128×64 | address 0x3C |
| 1 | SG90 / MG90S servo | if `z_mode=2` |
| 2–3 | Microswitch limit switches (NO) | X, Y (+ Z) |
| 3 | Momentary push buttons | Start, Stop, Home |
| 1 | Mushroom E-stop, normally closed | in the 12 V line |
| 1 | 12 V 5 A power supply | |
| 1 | LM2596 buck converter | set to 5.0 V **before** connecting |
| 2–4 | 100 µF 25 V electrolytic capacitors | one per driver |
| 3 | 10 kΩ resistors | limit pull-ups |
| 3 | 100 nF ceramic capacitors | limit filtering |
| | Wire, terminal blocks, perfboard or a CNC shield-style board | keep motor wires away from signal wires |

## Driver current (do this before connecting motors)

Vref = 8 × I_motor × R_sense, where R_sense is the small resistor printed on the module:

| Marking on module | R_sense | Vref for 1.0 A | Vref for 1.2 A |
|---|---|---|---|
| R100 (most clones) | 0.100 Ω | 0.80 V | 0.96 V |
| R068 (Pololu) | 0.068 Ω | 0.54 V | 0.65 V |
| R050 | 0.050 Ω | 0.40 V | 0.48 V |

Measure Vref between the pot's metal top and GND with 12 V on. Start around 1.0 A. If a motor misses steps, raise it in small steps; if the driver gets too hot to touch, lower it. Never unplug a motor while the driver is powered.

## Power

- 12 V → E-STOP → A4988 VMOT (all drivers) and the buck converter input.
- Buck 5 V → ESP32 VIN and servo V+. Do not run the servo from the ESP32's own 3.3 V.
- All grounds connected together.
- **Never connect the buck converter's 5 V to VIN while the USB cable is plugged into a laptop.** Many cheap DevKit boards have no protection diode, so the two supplies can push current back into the laptop's USB port; that can make the laptop shut down or restart. When flashing or using the USB console, **remove the 5 V → VIN wire** and power the board from USB only. Or use a powered USB hub, or a USB isolator for extra safety.
- The servo must get its 5 V from the buck converter, never from the ESP32's 5V/VIN pin while on USB. A servo stalling at power-up can draw over 0.5 A.
- In this layout the E-STOP cuts **everything**. After an E-stop, use `$RESUME` (or hold Start for 2 s) to continue from the saved nail.
