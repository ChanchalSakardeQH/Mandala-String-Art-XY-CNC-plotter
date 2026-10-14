# Plotter protocol v1 (for the Android app and the web dashboard)

The same text protocol runs on three links, which can all be open at once:

| Link | How to connect |
|---|---|
| Wi-Fi | TCP to port **23**. The hotspot `MandalaPlotter-XXXX` is at `192.168.4.1`; on the home network use the IP on the OLED or `mandala.local`. A new TCP connection replaces the old one. |
| Bluetooth | Classic SPP (serial port profile), device name `MandalaPlotter` (setting `bt_name`). Pair first. **Off by default** since firmware 1.1.2 (`$bt_enable=1` to switch on). Use it together with home Wi-Fi, not the hotspot: ESP32 hotspot + classic Bluetooth is unstable. |
| USB | Serial, 115200 baud. |
| HTTP (firmware ≥ 1.1.0) | Port **80**: the web dashboard and a small JSON/text API, described at the end of this file. |

Commands are lines ending in `\n`; `\r\n` also works. The plotter answers with lines ending in `\r\n`.

## Replies

| Line | Meaning |
|---|---|
| `ok` | the command was accepted. For G-code this means queued, not finished. |
| `error: <text>` | the command was refused; nothing changed |
| `ALARM:<n> <text>` | the machine stopped and locked; sent to every link |
| `[MSG:<text>]` | event or information; sent to every link |
| `<...>` | status report, the answer to `?` |

**Flow control.** Send a line, wait for its `ok` or `error`, then send the next. If the planner is full, the plotter simply holds the `ok` back until there is room. Lines may be at most 159 characters.

## Real-time keys

Send these as single characters, with no newline. They work at any time, except during an upload.

| Key | Action |
|---|---|
| `?` | status report |
| `!` | pause (feed hold). During a jog, it cancels the jog. |
| `~` | resume after `!`, or continue after a tie-on/tie-off pause (M0) |
| `0x18` (Ctrl-X) | stop the job or motion now. Progress is kept for `$RESUME`. |

## Status report

```
<Run|MPos:120.000,85.500,0.000|Job:square-weave.gcode,42.3|Step:3,17,112|H:1>
<Pause|MPos:40.000,75.103,0.000|Job:...|Msg:tie thread on at nail 112|H:1>
<Alarm|MPos:0.000,0.000,5.000|A:11|H:0>
```

- **State:** `Idle`, `Run`, `Hold` (paused or stopping), `Pause` (waiting for the operator, after M0), `Home`, or `Alarm`.
- **MPos:** machine position in mm. The origin is the home switches; Y points away from the operator.
- **Job:** file name and percent done.
- **Step:** run, step and nail.
- **Msg:** the operator message during a Pause.
- **A:** alarm code.
- **Pn:** limit switches currently pressed (X, Y, Z).
- **H:** `1` if homed.

Poll `?` about every 300–500 ms while a job runs.

## `$` commands

| Command | Action |
|---|---|
| `$` | help |
| `$I` | `[VER:MandalaPlotter 1.0.0]`, `[OPT:...]`, `[NET:ip=...,bt=...]` |
| `$$` | all settings as `$name=value` lines (passwords hidden) |
| `$name=value` | change a setting. Settings marked "after `$REBOOT`" reply with a MSG saying so. |
| `$H` | home all axes (`[MSG:Homed]` when done) |
| `$X` | unlock an alarm without homing (position is not trusted) |
| `$J=G91 X10 F1000` | jog (relative with G91, absolute with G90). Clamped to the travel limits once homed. |
| `$FILES` | `[FILE:name,size]` for each stored job, then `[FS:used,total]` |
| `$UPLOAD=name` … `$END` | store a file (see below) |
| `$ABORT` | during an upload: cancel it and delete the partial file |
| `$DEL=name` | delete a stored file |
| `$RUN=name` | start a stored job. If homing is required, it homes first. |
| `$STOP` | stop the job, keeping progress |
| `$RESUME` | continue the last stopped job from its saved nail |
| `$PROG` | `[PROG:file,run,step,nail]` or `[PROG:none]` |
| `$CLEARPROG` | forget the saved progress |
| `$MOTORS=0` / `$MOTORS=1` | disable / enable all motors (disabling loses homing) |
| `$RST=$` | restore default settings |
| `$RST=WIFI` | forget hotspot password, home Wi-Fi and Bluetooth settings (open hotspot after `$REBOOT`); also done by holding BOOT for 5 s |
| `$REBOOT` | restart |

File names may contain `A–Z a–z 0–9 . _ -` and be up to 31 characters long.

## Uploading a job

```
→ $UPLOAD=square-weave.gcode
← ok
→ G21 G90 G17 G94          (every line of the file, one at a time)
← ok
…
→ $END
← ok lines=726 bytes=25145
```

- Skip empty lines; the plotter ignores them, so they are not counted.
- `bytes` = the sum of (line length + 1) over all lines. Compare `lines` and `bytes` with what you sent, and if they differ, upload again.
- Real-time keys are switched off during an upload, so text like `!` inside comments is safe.
- Uploading over a file deletes any saved progress that belongs to it.
- Speed: roughly 4 s over Wi-Fi and 20–30 s over Bluetooth for a 700-line job. Keeping a few lines in flight (up to about 256 bytes not yet acknowledged) speeds this up.

## Job events (sent to every link)

```
[MSG:Homing]  [MSG:Homed]
[MSG:Job started square-weave.gcode]
[MSG:PAUSE tie thread on at nail 112]         ← show this to the user; continue with ~
[MSG:Hold]
[MSG:Job stopped (Stop button) at run=3 step=17 - $RESUME to continue]
[MSG:Resuming square-weave.gcode run=3 step=17]
[MSG:Job complete square-weave.gcode]
error: job line 214: arc end point not on circle (error 33)
```

## Alarms

| Code | Cause | How to clear |
|---|---|---|
| 1 | hard limit hit while moving, or stepper setup failed | `$H` (homing needed again) |
| 5 | stepper refused a move | `$H` or `$X` |
| 9 | homing failed (switch not found or stuck) | check the switch, then `$H` |
| 11 | homing required after power-up | `$H` (or `$X` to skip) |
| 12 | safe mode after 3 quick resets in a row (power problem); Wi-Fi, Bluetooth, servo and motors are off | fix the power, then `$REBOOT` |

## G-code the plotter accepts

| Code | Meaning |
|---|---|
| `G0` / `G1` | rapid move / move at feed F (mm/min) |
| `G2` / `G3` | clockwise / counter-clockwise arc, I/J centre offsets (R is not supported) |
| `G4 P<seconds>` | dwell |
| `G17` `G21` `G90` `G91` `G94` `G54` | accepted (G91 = relative moves) |
| `G28` | home (not allowed inside a job) |
| `M0` / `M1` | pause until `~` or Start. The comment after it is shown to the operator. |
| `M2` / `M30` | end of job |
| `M17` / `M18` | motors on / off |
| `M3` `M4` `M5` `M7` `M8` `M9` | ignored |

Z words control the lift: a real height with a Z stepper; with the servo, Z > `z_split` means up. A comment `; STEP run=R step=S nail=N` inside a job marks progress; these are written by the Kotlin core's `GcodeWriter`. Inches (`G20`) are refused.

**Marking jobs.** For each nail, write `G0 X Y`, `G1 Z0 F300`, `G4 P0.2`, `G0 Z5`. The marking-job generator will be added to the core module together with the Android app.

## HTTP API (firmware 1.1.0 and newer)

The plotter serves the dashboard at `http://192.168.4.1/` (hotspot) or `http://mandala.local/`.
Every answer carries `Access-Control-Allow-Origin: *`, so the same page opened as a local file can also connect.

| Request | Answer |
|---|---|
| `GET /` | the dashboard (gzipped HTML, embedded in the firmware) |
| `GET /api/status?since=N` | JSON status; `events` holds the broadcast lines newer than event number `N`; `seq` is the newest event number |
| `POST /api/cmd`, body = one command line | the reply lines as plain text (same as the TCP link). **503 `busy`** = planner full, nothing was done; retry shortly. |
| `POST /api/rt?k=hold\|resume\|stop\|status` | real-time key (`!`, `~`, Ctrl-X, `?`) |
| `POST /api/upload?name=F` (multipart/form-data, one file) | stores the file as job `F`. Answer: `{"ok":true,"name":"F","bytes":N}` or `{"ok":false,"error":"…"}` |

`$UPLOAD` is refused on `/api/cmd`; use `/api/upload` instead, which is much faster.

**Captive portal** (firmware 1.2.0, setting `captive=1`). On the hotspot a DNS server answers every name with 192.168.4.1. Any HTTP request for another host name, such as the operating system's connectivity checks, is redirected to `http://192.168.4.1/`, so the dashboard opens by itself after joining.

**Theme.** `"theme"` in the status is the dashboard theme stored on the plotter (`$ui_theme=walnut`). The dashboard applies it when it connects and saves it there whenever the user changes theme.

Example status:

```json
{"ver":"1.1.0","state":"Run","x":120.5,"y":88.0,"z":0.0,"homed":true,"alarm":0,"msg":"",
 "job":{"active":true,"pct":42.3,"run":3,"step":17,"nail":112,"name":"lotus-528n-wind.gcode"},
 "saved":"[PROG:lotus-528n-wind.gcode,3,17,112]","pins":"","ip":"192.168.4.1","bt":false,"tcp":false,
 "theme":"walnut","zMode":2,"xMax":400.0,"yMax":400.0,"seq":57,"events":["[MSG:Job started lotus-528n-wind.gcode]"]}
```

Note: the web server runs in the same loop as the motion control. Each request takes a few milliseconds; loading the full page takes a fraction of a second, which can add a short pause at a nail. Avoid reloading the page while a job runs. Status polling is fine.
