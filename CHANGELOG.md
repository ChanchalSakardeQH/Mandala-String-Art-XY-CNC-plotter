# Changelog – Mandala String Art XY Plotter (woodyouloveit)

All notable changes to every part of the project are recorded here, newest first.
Each part has its own version number:

| Part | Folder | Current version |
|---|---|---|
| Web dashboard | `web/` | 2.3.0 |
| ESP32 firmware | `firmware/` | 1.3.0 |
| Design core (Kotlin, used by the Android app) | `stringart-core/` | 0.4.0 |
| Android app | `android/` | not started |

Versions follow semantic versioning: MAJOR changes break files or commands, MINOR adds features, PATCH fixes bugs.
The web page shows its own entries under "What's new"; keep that list (`CHANGELOG` in `web/index.html`) in step with this file.

---

## Project

### 2026-10-08 – one project folder
- All parts moved into one folder with a single changelog: `firmware/`, `web/`, `stringart-core/`, `docs/` (shared by firmware and app), and `android/` (next).
- Firmware sketch renamed to `firmware/MandalaPlotterESP32/MandalaPlotterESP32.ino`. The sketch folder name must match the .ino name.
- The tested FastAccelStepper 2.0.0 is kept in `firmware/libraries/`.
- The Android project will use `stringart-core` in place (`projectDir = file("../stringart-core")`) instead of a copy.

---

## Web dashboard (was: web template designer)

### 2.3.0 – 2026-10-09
**Added**
- **Plotter step makes both jobs at once:** Job 1 marks every nail, Job 2 winds the thread.
  - Summary cards like the String Art Generator: time for each job, total machine time with the number of pauses, and thread needed plus how much to buy (15% spare).
  - The Job 1 / Job 2 switch picks which one to simulate or send.
  - "Download nail G-code" and "Download winding G-code" buttons.
- **New simulation on a picture of the machine:**
  - the bed with a 50 mm grid and the board in its real position;
  - the X rail with end blocks and the carriage + crosshair following the needle (red when the needle is down);
  - X/Y axis marks at home;
  - a dark **pause bubble** at every tie-on/tie-off;
  - optional guide path;
  - speeds 1× to 1000× from a list;
  - status like "String 105 of 419 · thread 40 of 62 · nail 25";
  - a stats line with G-code lines, run time, guide-path length and the X/Y travel range.
- **Machine presets:** the board is centred on a 400 × 400 mm machine by default; machine size and centring are in the Plotter step, and the size is taken from the plotter when connected.
- **Thread thickness** (Colours) shows how dark the finished board will look, in the preview, picture exports and simulation.
- **Brand mark on everything you download or share:**
  - "wood♥it" + woodyouloveit.com on every PDF page and on SVG templates;
  - a white band with the mark under PNG/SVG pictures;
  - a comment line in CSV and G-code;
  - a `brand` field in design files;
  - the ZIP how-to.
- **Machine setup:** the hotspot can be left open or have its password removed, with a note on the BOOT-button reset.

**Changed**
- Footer buttons wrap long labels.


### 2.2.0 – 2026-10-09
**Added**
- **Machine setup** (step 4) in plain words, saved on the plotter:
  - work area;
  - motor drive (GT2 20T / 16T / T8 lead screw / custom) with **Test X / Test Y** moves and Reverse switches;
  - second Y motor;
  - pen/needle lift (none / servo / stepper) with servo angles and a **down-then-up test**;
  - limit switches (on/off, and which side each one is on);
  - speed presets (Gentle / Normal / Fast);
  - hotspot password, home Wi-Fi, auto-open, Bluetooth, Wi-Fi strength.

  Only changed values are sent. A **Restart plotter** button appears when a change needs it. Every raw `$` setting and a factory reset are under **Advanced**.
- With no limit switches configured, the status badge shows **Ready** instead of "Not homed", and the live head position is shown.

**Fixed**
- Screen updates and simulation playback could stall where the browser throttles animation frames (seen in automated tests; can also happen in phone sign-in windows). Both now fall back to a short timer.


### 2.1.0 – 2026-10-09
**Added**
- **Simulation** before running the plotter. It plays the generated G-code itself (needle, thread, wrap loops, pen down/up, tie-on stops shown as "Plotter waits for you: …").
  - Speed ×1, ×5, ×10, ×25, ×50, ×100 or ×250, with the resulting playback time shown.
  - Position slider; current thread, step and nail.
  - The Plotter step requires it: Generate → Simulate → Send.
- **Guided flow:** every step ends with Next (Design → Template → Plotter → Machine). The Machine step offers "Send job to plotter" when a simulated job hasn't been sent yet.
- **Seven themes:** Woody, Night, Walnut, Ocean, Forest, Rose, High contrast, chosen from a menu. The choice is kept in the browser and stored on the plotter (`$ui_theme`), so every phone or PC gets it back after a restart.

**Changed**
- **Design step simplified.** "Start from a design" and the "Pattern" list are merged into one gallery of all 16 designs, grouped Rings / Shapes. Each tile loads its best settings; then "Adjust <design>" shows only the sliders that design uses. Reset returns the current design to its starting settings.
- Old brand text removed (page, template, G-code header, share message). The design file format is now `woodyouloveit-mandala-design`.
- The alarm box in the Machine step offers **Home** for homing alarms instead of Continue.

**Checked**
- End to end against a simulated plotter: flow, simulation, upload, start, theme persistence and plotter theme sync, no errors.
- G-code is still identical to the Kotlin core on 12 preset/option combinations.


### 2.0.1 – 2026-10-08
- Shows the firmware's safe-mode alarm (12) with what to do.

### 2.0.0 – 2026-10-08
**Added**
- Dashboard in four steps (Design, Template, Plotter, Machine), in the woodyouloveit String Art Generator style: sidebar with numbered steps and pinned actions, graph-paper canvas with ruler, light/dark theme, mm/cm/inch.
- Served by the plotter itself at http://192.168.4.1 (firmware 1.1.0). It also still works as a local file and can connect to the plotter by address.
- **Plotter step:**
  - winding job: thread runs, wrap directions, collision checks, leaving out strings that can't be wound, joining loose ends;
  - nail-marking job;
  - result summary, G-code save/share, and send to the plotter with upload progress.
- Planner and G-code ported 1:1 from `stringart-core`. On 12 preset/option combinations the output is identical to the Kotlin core, line by line and number by number. The G-code the page uploads matched the Kotlin reference byte for byte in the end-to-end test.
- Preview modes: Design, Template (real print layout) and Thread path (each thread in its own colour, left-out strings dashed red, live head position).
- **Machine step:** status card with progress and operator messages; Home, Pause, Continue, Unlock, Resume, Stop; jog pad with step sizes; stored jobs (run/delete); settings editor; console.

**Changed**
- The design check now says strings "pass very close" to a nail; the exact check happens in the Plotter step.

### 1.0.0 – 2026-10-08
**Added**
- Single offline HTML page. It needs no internet, and it saves the last design in the browser.
- All 16 patterns and the 14 reference designs from web tool V1.2, with thumbnail gallery.
- Geometry ported 1:1 from `stringart-core`. All 14 presets were checked identical to the Kotlin output: same nails, same nail numbers, same strings.
- Machine check, with needle diameter and clearance as settings:
  - nails too close for the needle;
  - strings that run over another nail (can be marked in red);
  - nails with no string.
- Print template at real size, as PDF or SVG:
  - one sheet, or tiled on A4, A3 or Letter, with automatic portrait/landscape choice;
  - 12 mm overlap with dashed trim lines and (+) alignment marks kept clear of nails;
  - page map on every page;
  - nail numbers (all, every 5th, or none);
  - ring guides, centre mark, title block, and a 100 mm check bar.
- PDF writer built in, vector and with no libraries. The drawing is stored once and reused on every page to keep files small (4 A4 pages of Lotus ≈ 200 KB).
- Coloured picture as PNG (2000 px) and SVG, nail list as CSV, and a design file (save and open).
- Share to WhatsApp / email through the phone's share menu. Where a browser can't attach the file, it is downloaded and WhatsApp / email open with a ready message.

**Changed (compared with web tool V1.2)**
- Every string ends on a real nail. V1.2 attached most strings in Vertical Diamond, Hex Flower, Layered Star, Triangle Portal and Heart Web to points that were never nails.
- Shared corners are one nail and duplicate strings are merged. Square Weave had 68 duplicates out of 240.
- Only the settings a pattern really uses are shown. In V1.2, Frame, Rings, Ring gap and Rotation silently did nothing for some patterns.
- Margins in shape patterns scale with board width; they were fixed millimetres.

**Fixed (bugs in V1.2)**
- SVG export no longer crashes for the 10 ring patterns.
- Nails are drawn in the preview for ring patterns, and the nail count is correct. V1.2 counted rings instead of nails.
- Crown pattern no longer produces invalid nail indices with large skip values.

---

## ESP32 firmware

### 1.3.0 – 2026-10-09
**Changed**
- **No Wi-Fi password after flashing:** the hotspot `MandalaPlotter-XXXX` is open until a password is set in Machine setup (`ap_pass` default empty; 8+ characters when set).
- **More reliable auto-open of the dashboard:**
  - the hotspot is now configured explicitly with the plotter as DNS server;
  - on ESP32 core 3.x the DHCP captive-portal option (RFC 8910) is also sent, which Android 11+ and iOS 14+ use to show the dashboard straight away;
  - on core 2.0.17 the DNS redirect still triggers Windows, Android and iOS sign-in detection.

**Added**
- **Forgot the password?** Hold the **BOOT** button on the ESP32 for 5 seconds. The OLED counts down, then the plotter clears only the network settings (hotspot password, home Wi-Fi, Bluetooth, Wi-Fi strength, auto-open) and restarts as an open hotspot. Machine settings stay. Also available as `$RST=WIFI`.
- The start-up line says whether the hotspot is open or has a password.

**Defaults** (unchanged, listed for reference): work area 400 × 400 mm, GT2 belt / 20-tooth pulley (80 steps/mm), servo lift, Normal speed, no limit switches, Bluetooth off.

**Checked**
- Compiles with no warnings on core 2.0.17 (ESP32-WROOM-DA, 1.75 MB) and 3.3.12 (1.93 MB), Huge APP partition.


### 1.2.1 – 2026-10-09
**Changed**
- **Beginner defaults: no limit switches assumed** (`homing_enable=0`, `hard_limits=0`). The plotter starts **Ready** instead of in `ALARM:11`, and prints a single `[MSG:Ready]`. Switch homing on in Machine setup once switches are fitted.
- When homing is on, the start-up message is plainer ("Home the machine first: press Home").
- OLED shows `!HM` / "Press Home to start" only when homing is actually required.
- `/api/status` reports `"homing"` (0/1).

**Note**
- A screen full of repeated `…$X to skip` lines right after an upload, with no boot messages in between, came from the Arduino IDE Serial Monitor / USB-serial link, not from the plotter. The firmware prints that message once per boot (see Troubleshooting).


### 1.2.0 – 2026-10-09
**Added**
- **The dashboard opens by itself** after joining the plotter's Wi-Fi (captive portal).
  - A small DNS server points every name to the plotter, and connectivity checks are redirected to http://192.168.4.1/.
  - Windows opens the default browser; Android and iPhone show a "Sign in to network" page with the dashboard.
  - Setting `captive` (default 1).
- Setting `ui_theme`: the dashboard theme, kept on the plotter. `/api/status` reports it.

**Changed**
- Brand text removed from the firmware header.

**Checked**
- Compiles with no warnings on core 2.0.17 (ESP32-WROOM-DA) and 3.3.12, Huge APP partition. The dashboard is 43 KB gzipped.


### 1.1.2 – 2026-10-09
**Fixed**
- Dashboard at http://192.168.4.1 timed out ("taking too long to respond") with only the laptop connected.
  - Cause: classic Bluetooth, always discoverable, was sharing the radio with the Wi-Fi hotspot. Espressif rates "SoftAP connected + BR/EDR" as *C1: supported but the performance is unstable*.
  - Bluetooth is now **off by default** (`bt_enable=0`), and Wi-Fi stays awake when Bluetooth is off.
  - For Bluetooth, use home Wi-Fi (STA + BR/EDR is rated *stable*).

**Added**
- Console messages when a device joins or leaves the hotspot, and when the dashboard page is requested and sent (with time taken), to make connection problems visible.
- Troubleshooting section "http://192.168.4.1 doesn't load" in `firmware/README.md`.

**Checked**
- Compiles with no warnings on core 2.0.17 (ESP32-WROOM-DA and Dev Module) and 3.3.12, Huge APP partition.

### 1.1.1 – 2026-10-08
**Fixed**
- Reboot loop on USB power. At every boot the firmware drove the servo to "up" at the same moment Wi-Fi and Bluetooth started, and the combined current spike could reset the board, which then repeated. The servo now gets no pulse until the first Z move or homing.

**Added**
- Every boot prints why the last reset happened (`[MSG:Last reset: BROWNOUT …]`, crash, watchdog, power-on, …).
- Boot-loop guard: after 3 resets within 15 s of boot, the plotter starts in **safe mode** (`ALARM:12`). Wi-Fi, Bluetooth, servo and motors stay off and the loop stops; `$REBOOT` once the cause is fixed.
- Setting `wifi_dbm` (default 11 dBm instead of the maximum 19.5): lower current peaks with the same workshop range.
- Setting `bt_enable` (0 = Bluetooth off).
- Bluetooth now starts 0.3 s after Wi-Fi so the two current peaks don't overlap.

**Docs**
- `docs/WIRING.md` wrongly said "USB and VIN together is fine for flashing". Corrected: never connect the buck's 5 V to VIN while USB is plugged into a PC, because it can backfeed the laptop's USB port.
- New troubleshooting section in `firmware/README.md`.

**Checked**
- Compiles on core 2.0.17 (ESP32 Dev Module and ESP32-WROOM-DA) and 3.3.12, Huge APP partition.

### 1.1.0 – 2026-10-08
**Added**
- Web server on port 80. Before this, http://192.168.4.1 showed "refused to connect" because 1.0.x only listened on TCP port 23.
- The dashboard is embedded gzipped (37 KB) and rebuilt by `tools/embed_web.py` from `web/index.html`.
- HTTP API:
  - `/api/status` (JSON, with an event log);
  - `/api/cmd` (any command line);
  - `/api/rt` (pause/continue/stop);
  - `/api/upload` (multipart job upload).
- CORS enabled, so the local-file dashboard can connect too.
- mDNS advertises `http` on `mandala.local`. The boot message shows the dashboard address.

**Checked**
- Compiles with no warnings on ESP32 core 2.0.17 (1.73 MB) and 3.3.12 (1.90 MB), Huge APP partition.
- The dashboard's API client was tested end to end against a simulated plotter.

### 1.0.1 – 2026-10-08
**Fixed**
- Compile error `no matching function for call to Button::Button(...)` in `Ui.cpp` on ESP32 core 2.x. Core 2.x compiles as C++11, which does not allow `Button{PIN}` for a struct with default member values; `Button` now has a constructor.

**Changed**
- Compile-tested on ESP32 core **2.0.17** (1.66 MB) and **3.3.12** (1.83 MB). Both need the "Huge APP" partition scheme.

### 1.0.0 – 2026-10-08
**Added**
- Target: ESP32-WROOM-32, A4988 + NEMA17, SSD1306 OLED. Compile-tested with ESP32 core 3.3.12 and FastAccelStepper 2.0.0. Requires the "Huge APP" partition scheme.
- Motor setups selectable by setting: X+Y, X + dual Y (second driver), Z stepper, or Z servo.
- Coordinated straight moves and nail-wrapping arcs (G0/G1/G2/G3), dwell, pause for tie-on/tie-off (M0), end (M2).
- Homing with seek / back-off / slow locate, configurable switch side, hard and soft limits.
- Wi-Fi hotspot plus optional home Wi-Fi (TCP port 23, `mandala.local`), Bluetooth serial and USB, all usable at the same time.
- Jobs are uploaded to flash and run on the ESP32. Progress is saved to flash. After a power cut or Stop, the machine homes, returns to the saved nail, and asks the operator to re-tie.
- OLED status screen; Start/Pause, Stop and Home buttons; status LED.
- All settings changeable with `$name=value`, stored in flash.
- Wiring diagram (SVG/PNG), pin table, parts list, protocol reference, and a PC upload tool (`tools/send_job.py`).

**Known limits**
- Not yet tested on hardware.
- Arcs run as short straight segments with a brief stop between them.
- No automatic squaring for dual Y.

---

## Design core (`stringart-core`, Kotlin)

### 0.4.0 – 2026-10-09
- The G-code starts with the brand line `; woodyouloveit.com - Mandala String Art`.
- Output is still identical to the web dashboard on all 12 test cases; 21 tests pass.


### 0.3.0 – 2026-10-09
**Changed**
- Package renamed to `com.woodyouloveit.stringart.core` (old brand removed from the name).
- The G-code header now reads `; <name> - mandala string art job v1`.
- Job format ID is now `woodyouloveit-stringart-job`.
- 21 tests pass; output still identical to the web dashboard.


### 0.2.0 – 2026-10-08
**Changed**
- Distances use `sqrt(dx² + dy²)` instead of `hypot`, which is bit-identical on the JVM, Android and JavaScript.
- Near-ties (within 1e-7) in connector order, odd-nail pairing and wrap-direction choice resolve to the first candidate.
- The web dashboard and the Android app now plan exactly the same jobs. Before, symmetric designs could split ties differently and give different routes.
- Side effect: connectors cut more tie-ons (Square Weave 13 → 7 threads, Golden Star 29 → 11 with joining up to 10 mm).
- All 21 tests pass.

### 0.1.0 – 2026-10-08
**Added**
- Port of the 16 patterns with real nail indices, nail de-duplication and scaled margins.
- Route planner:
  - minimum number of thread runs (Euler trails), with optional short connector strings to reduce tie-offs;
  - wrap direction per nail chosen to keep needle and thread clear of other nails.
- Validator: nail spacing, board fit, exact needle/thread/wrap-arc collisions, optional auto-removal of strings that can't be wound.
- Job format v1 (JSON) and a G-code writer for GRBL/FluidNC/this firmware:
  - `; STEP` markers and resume from any step;
  - Y flip in one place only.
- 21 unit tests; no Android or third-party dependencies.

**Known limits**
- Multi-ring presets lose many strings to auto-fix because their strings cross inner-ring nails. These patterns need redesigning for winding.
- The marking-job (pen dot per nail) generator is not written yet; it is planned together with the Android app.
