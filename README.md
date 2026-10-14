# Mandala String Art XY Plotter – woodyouloveit

This project designs mandala string art, prints the nail template, marks the nail positions, and winds the thread on an ESP32 XY plotter.

```
mandala-string-art/
├── README.md              ← you are here
├── CHANGELOG.md           ← one changelog for every part
├── docs/                  ← shared by firmware and app
│   ├── wiring-diagram.svg / .png
│   ├── WIRING.md          pin table, parts list, driver current
│   ├── PROTOCOL.md        commands between app and plotter
│   └── JOB_FORMAT.md      the job file the app creates
├── firmware/              ← ESP32 (Arduino IDE)
│   ├── MandalaPlotterESP32/   open MandalaPlotterESP32.ino
│   ├── libraries/             FastAccelStepper-2.0.0.zip (tested copy)
│   ├── tools/send_job.py      PC test uploader
│   ├── tools/embed_web.py     copies web/index.html into the firmware
│   └── README.md              setup, first power-up, settings
├── web/                   ← dashboard: design, template, plotter jobs, machine control
│   ├── index.html         (also served by the plotter at http://192.168.4.1)
│   └── README.md
├── stringart-core/        ← Kotlin design engine (used by the app)
└── android/               ← Android Studio project (next)
```

## How the parts work together

1. **Design:** in the dashboard (join the plotter's Wi-Fi and it opens by itself, or open http://192.168.4.1, or `web/index.html`) or in the Android app. Both use the same geometry, so nail numbers match everywhere.
2. **Template:** print the PDF from the web page and fix it on the board. Or let the plotter mark the nails, if a Z servo or Z stepper is fitted.
3. **Nails:** hammer them in through the marks.
4. **Wind:** the dashboard (step 3) or the app plans the thread, checks it, and sends the job to the plotter. The plotter winds it on its own, and pauses for tie-on and tie-off. Step 4 of the dashboard controls the machine.

## Versions

| Part | Version | Tested with |
|---|---|---|
| Web dashboard | 2.3.0 | Chrome desktop and mobile; end-to-end against a simulated plotter; G-code identical to the Kotlin core |
| Firmware | 1.3.0 | compiles on ESP32 core 2.0.17 and 3.3.12; boots and answers on your board (1.0.1); dashboard not yet tried on hardware |
| stringart-core | 0.4.0 | 21 unit tests |
| Android app | – | not started |

Every change goes into `CHANGELOG.md` with a new version number for the part that changed.

## Working on Windows

- Keep the project at a short path such as `C:\Projects\mandala-string-art`. Gradle builds can fail with very long paths like `C:\Users\…\Downloads\…\…`.
- Arduino IDE: open `firmware\MandalaPlotterESP32\MandalaPlotterESP32.ino` directly. If you rename the sketch folder, rename the `.ino` to the same name.
- Don't keep zips of a part next to its folder; build them only when sending a release.
