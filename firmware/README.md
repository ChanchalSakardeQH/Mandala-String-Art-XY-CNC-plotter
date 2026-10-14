# Mandala String Art XY Plotter — ESP32 firmware v1.3.0

The firmware marks nail positions and winds thread from G-code jobs stored on the ESP32. You control it from the Android app over Bluetooth or Wi-Fi, from the three buttons on the machine, or from a PC over USB.

| File | What it does |
|---|---|
| `MandalaPlotterESP32/MandalaPlotterESP32.ino` | setup / loop (the sketch folder name must match the .ino name) |
| `Web.*` | web dashboard and HTTP API on port 80 |
| `WebPage.h` | the dashboard page, gzipped. **Generated** from `web/index.html` by `tools/embed_web.py`; don't edit it by hand |
| `Config.h` | pin numbers, sizes |
| `Settings.*` | all `$` settings, saved in flash |
| `Motion.*` | moves, arcs, servo / Z stepper, pause / resume, homing, limit switches |
| `Gcode.*` | G-code parser |
| `Job.*` | runs a stored job, saves progress, resumes after a power cut |
| `Comms.*` | USB, Bluetooth, Wi-Fi; `$` commands; file upload |
| `Ui.*` | OLED screen, buttons, LED |
| `../docs/wiring-diagram.svg` / `.png` | circuit diagram |
| `../docs/WIRING.md` | pin table, parts list, power notes |
| `../docs/PROTOCOL.md` | command reference (for the Android app) |
| `libraries/FastAccelStepper-2.0.0.zip` | the exact stepper library version tested |
| `tools/send_job.py` | PC tool to upload and run a job before the app exists |
| `tools/embed_web.py` | rebuilds `WebPage.h` after `web/index.html` changes |

## 1. Arduino IDE setup (Windows)

1. **File → Preferences → Additional boards manager URLs:**
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. **Boards Manager:** install **esp32 by Espressif**, version **2.0.17** or **3.x**. The firmware is compile-tested on both 2.0.17 and 3.3.12.
3. **Library Manager:** install these.
   - **FastAccelStepper** 2.0.0 or newer. Older versions had a bug on ESP32 core 3.x where a second stepper could keep running. To use the tested copy instead, choose Sketch → Include Library → Add .ZIP Library and pick `libraries/FastAccelStepper-2.0.0.zip`.
   - **Adafruit SSD1306**. Accept its dependencies, Adafruit GFX and BusIO.
4. **Tools menu:**
   - Board: **ESP32 Dev Module**
   - Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)**. This is required: the firmware is 1.8 MB because Wi-Fi and Bluetooth are both included, and it does not fit the default scheme.
   - Upload speed: 921600. If uploads fail, use 115200.
5. Open `MandalaPlotterESP32/MandalaPlotterESP32.ino` and click Upload. Then open the Serial Monitor at 115200 baud with "Both NL & CR".

On core 3.x you will see a warning that BluetoothSerial is "deprecated… won't be supported in 4.0.0". It is harmless on 3.x. Don't move to core 4.0 until a later firmware version supports it.

The board must be an **ESP32-WROOM-32 (classic ESP32)**. ESP32-S3 and C3 have no classic Bluetooth, and the sketch stops with a clear compile error if you pick one of them.

## 2. First power-up, step by step (all from the dashboard)

Do these in order. Keep the belts loose, or the motors unplugged, until step 3.

1. **Board only, on USB.** The console shows `[MSG:Ready]`. Join the Wi-Fi `MandalaPlotter-XXXX` with your phone or laptop and the dashboard opens; otherwise go to http://192.168.4.1. Go to **4 Machine**.
2. **Driver current.** With 12 V on and the motors unplugged, set the pot on each A4988 (see `../docs/WIRING.md`).
3. **Motors.** In **Machine setup → Motors**, pick your drive (GT2 belt, 20-tooth pulley for most builds) and press **Test X** and **Test Y**. Each axis should move 10 mm away from the home corner (front left). If one goes the wrong way, tick **Reverse**, press **Save changes**, then **Restart plotter**.
4. **Check the distance.** Use the jog pad to move 50 mm and measure. If it's wrong, choose **Other** in Drive and enter steps/mm = current value × 50 ÷ measured.
5. **Pen / needle lift.** Choose **Servo** (or **Stepper**), set the up/down angles and press **Test: down, then up**.
6. **Limit switches (optional).** Once fitted, tick **My machine has limit switches**, choose the side each switch is on, save, and press **Home**. Without switches, park the head at the front-left corner before switching on.
7. **Work area and speed.** Enter the real travel, and start with **Gentle** speed.
8. **First job without thread.** In **3 Plotter**: Generate job → Simulate → Send to plotter, then **Start job**.

All of these values can also be set with `$` commands from the USB console; see the settings table below.

## Web dashboard

1. Join the plotter's Wi-Fi `MandalaPlotter-XXXX`. It has **no password** until you set one in Machine setup. **The dashboard opens by itself:**
   - Windows opens your default browser.
   - Android and iPhone show a "Sign in to network" page with the dashboard. On Android, use ⋮ → *Open in browser* if you want to save files; downloads don't work inside the sign-in page.
   - If nothing opens, go to **http://192.168.4.1**. On your home Wi-Fi, use the IP shown on the OLED, or `http://mandala.local`.
2. Leave the hotspot connected even though Windows or Android says "No internet".
3. Work through the four steps:
   - **Design:** pick and adjust the mandala.
   - **Template:** print it or share it.
   - **Plotter:** make the winding or marking job and send it to the plotter.
   - **Machine:** home, start, pause, continue, stop, resume, jog, manage stored jobs, change settings, use the console.

After changing `web/index.html`, run `python firmware/tools/embed_web.py` and flash again.

## 3. Using the machine

**Buttons**
- **Start** (short press): pause and resume. It also continues after a tie-on/tie-off stop.
- **Start** (hold 2 s): resume the last stopped job from its saved nail.
- **Stop**: stop the job. Progress is kept.
- **Home**: home all axes.

**OLED**
- Top line: connections (AP / WiFi, BT, TCP).
- Middle: the state, or the job percentage while running.
- Then the position, and either the run / step / nail or the message to the operator (e.g. "tie thread on at nail 112").
- Bottom line: the IP address.
- `!HM` means the machine is not homed.

**LED:** steady = app connected, slow blink = running, fast blink = alarm.

**Power cut or Stop.** Progress is saved at the start of every thread and at least every 2 s. After power returns, hold Start for 2 s (or send `$RESUME`). The machine homes, lifts, travels to where it stopped, lowers, and pauses with "Tie thread at nail N". Press Start to continue.

**Home Wi-Fi.** To join your router as well as running the hotspot:
`$wifi_ssid=MyWiFi`, `$wifi_pass=secret`, `$REBOOT`. The router's IP for the plotter is shown on the OLED, and it also answers at `mandala.local`.

## 4. Settings (`$$` lists them, `$name=value` changes them)

| Setting | Default | Meaning |
|---|---|---|
| `x_steps_mm`, `y_steps_mm` | 80 | GT2 belt, 20T pulley, 1/16 microstep |
| `z_steps_mm` | 400 | Z stepper (e.g. T8 lead screw, 1/16) |
| `x_max`, `y_max`, `z_max` | 400, 400, 10 | travel (mm) |
| `x_dir_inv`, `y_dir_inv`, `y2_dir_inv`, `z_dir_inv` | 0,0,1,0 | reverse a motor * |
| `y_dual` | 0 | 1 = second Y driver on GPIO32/14 * |
| `z_mode` | 2 | 0 none, 1 Z stepper, 2 servo * |
| `servo_up`, `servo_down`, `servo_ms` | 90, 30, 250 | servo angles and settle time |
| `z_split` | 0.5 | servo: Z above this = up |
| `z_safe` | 5 | Z "up" height (power-up, resume) |
| `max_rate_xy`, `max_rate_z` | 6000, 600 | mm/min |
| `accel_xy`, `accel_z` | 1500, 300 | mm/s² |
| `arc_rate` | 1200 | speed limit while wrapping a nail (mm/min) |
| `arc_tol` | 0.01 | arc accuracy (mm) |
| `homing_enable`, `homing_seek`, `homing_feed`, `homing_pulloff` | **0**, 1500, 150, 2 | homing (off until limit switches are fitted; switch on in Machine setup) |
| `home_dir_mask` | 0 | bit 1 = X, 2 = Y, 4 = Z switch at max end |
| `home_z` | 0 | home the Z stepper too |
| `limit_invert` | 0 | 1 for normally-closed switches |
| `hard_limits`, `soft_limits` | **0**, 1 | stop on switch hit / refuse moves outside travel |
| `enable_invert` | 0 | 0 for A4988 * |
| `wifi_ssid`, `wifi_pass`, `ap_pass`, `bt_name` | –, –, *(empty = open hotspot)*, MandalaPlotter | network * |
| `captive` | 1 | 1 = joining the hotspot opens the dashboard automatically; 0 = off * |
| `ui_theme` | light | dashboard theme (woody/light, dark, walnut, ocean, forest, rose, contrast), set from the dashboard |
| `wifi_dbm` | 11 | Wi-Fi transmit power, 2–20 dBm. Lower = smaller current peaks; 11 easily covers a workshop * |
| `bt_enable` | **0** | 1 switches classic Bluetooth on. Leave it off when you use the hotspot (see below) * |

\* takes effect after `$REBOOT`. `$RST=$` restores all defaults.

## Forgot the hotspot password?

Hold the **BOOT** button on the ESP32 board for **5 seconds** while the plotter is idle. The OLED counts down "Wi-Fi reset in 5s…". The plotter then forgets the hotspot password, home Wi-Fi, Bluetooth, Wi-Fi strength and auto-open settings, and restarts as an **open** hotspot. Work area, motors, servo and speed settings are kept. From the USB console, `$RST=WIFI` then `$REBOOT` does the same.

## Troubleshooting: http://192.168.4.1 doesn't load

1. The console should show `[MSG:Wi-Fi: a device joined the hotspot …]` when the laptop or phone joins `MandalaPlotter-XXXX`. If it doesn't appear, the device isn't really on the hotspot. Forget the network and join again.
2. On opening the page the console shows `[MSG:Web: page requested by 192.168.4.x]` and then `page sent, … bytes in … ms`. If "requested" appears but "sent" takes many seconds, the radio is overloaded; check point 3.
3. **Bluetooth and the Wi-Fi hotspot do not work well together on the ESP32.** Espressif's coexistence table rates "SoftAP connected + classic Bluetooth" as *supported but unstable*. Pages then time out. That's why Bluetooth is **off by default** since 1.1.2 (`$bt_enable=0`).
   - If you need Bluetooth for the app, connect the plotter to your **home Wi-Fi** instead (`$wifi_ssid=…`, `$wifi_pass=…`, `$REBOOT`) and open the dashboard at the IP on the OLED. Home Wi-Fi + Bluetooth is rated *stable*.
4. Type `http://192.168.4.1`, not https. In Firefox, if "HTTPS-Only Mode" is on, allow the exception, or use Chrome/Edge.
5. Windows may say "No Internet" for the hotspot. That's normal; the plotter has no internet.

## Easiest setup: the dashboard

Everything in the settings table below can be changed in the dashboard under **4 Machine → Machine setup**, in plain words with test buttons. The table is for reference.

## Troubleshooting: the Serial Monitor repeats one line endlessly

The firmware prints its start-up messages **once** per boot. If the Serial Monitor fills with the same line (for example `…$X to skip`) and there are **no** `ets …`/`rst:` boot lines between the copies, the board is not restarting; the repeats come from the PC side:
- Close the Serial Monitor before uploading, and reopen it after the upload finishes.
- Use a released Arduino IDE (e.g. 2.3.x) rather than a "nightly" build.
- Update the USB-serial driver for your board's chip (CH340/CH341 or CP210x). Old CH340 drivers are also known to cause Windows crashes.
- Press the board's EN/RST button to see a clean start.

## Troubleshooting: reboot loop, or the PC restarts

If the console keeps repeating the start-up lines, the board is resetting. Every boot now prints the reason as `[MSG:Last reset: …]`:

| Reset reason | Meaning |
|---|---|
| `BROWNOUT` | The supply voltage dropped. Remove everything except the bare board and try again with a short, good USB cable on a different port, or a powered hub. Then add parts back one at a time. A servo or OLED powered from the board is the usual cause. |
| `CRASH` / `WATCHDOG` | A software fault. Copy the lines printed just before the reset and send them. |
| `power-on` repeating | The USB port itself is cutting power. Unplug immediately and check the wiring for a short or for backfeed (see `docs/WIRING.md` → Power). |

After **3 quick resets in a row** the plotter starts in **safe mode**: Wi-Fi, Bluetooth, servo and motors stay off, the OLED and the USB console show `ALARM:12`, and the reboot loop stops. Fix the cause, then send `$REBOOT` (or press the board's EN button). A boot that runs for 15 seconds clears the counter.

**If the laptop itself shuts down or restarts, stop using it with the plotter connected** until the wiring is checked:
- Is the buck converter's 5 V connected to VIN while USB is plugged in? Disconnect it.
- Is anything shorted? With all power off, measure the resistance between 3V3 and GND, and between 5V and GND; it must not be close to 0 Ω.
- Is the servo or a motor driver powered from the board's pins?

## 5. Known limits of v1.0

- **Compile-tested, not yet hardware-tested.** The G-code parser was also checked on a PC against a real 726-line winding job (0 errors, every arc closes within 0.0012 mm). Expect some tuning on your machine.
- Wrapping arcs are run as short straight pieces with a tiny stop between each one. That is simple and accurate, but not the fastest possible. Smoother continuous arcs can come in v1.1 once the basics are proven.
- Dual Y has no automatic squaring: square the gantry by hand before homing.
- The E-STOP must cut power in hardware. The Stop button is not a safety device.
