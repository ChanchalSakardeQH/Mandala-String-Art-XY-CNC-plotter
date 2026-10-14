# Mandala String Art dashboard (web) – v2.1.0

`index.html` is a single offline page with four steps:
1. **Design:** pick and adjust the mandala.
2. **Template:** print it or share it.
3. **Plotter:** make the winding or marking job.
4. **Machine:** control the plotter.

It has no installation, no internet requirement and no external files.

The plotter serves this same page at **http://192.168.4.1** (firmware 1.1.0+). You can also open the file locally and connect to the plotter's address in step 4.

## Use it

- **On a PC:** double-click `index.html` (Chrome, Edge or Firefox).
- **On a phone:** host the file (below) and open the link. You can also add it to the home screen.
- **Host it:** upload `index.html` to any static host. GitHub Pages, Netlify or your own site all work, and nothing else needs to be uploaded. Use **https**: the phone share menu only works on secure pages. Note that a page on https cannot reach the plotter (browsers block http from https); for machine control, use http://192.168.4.1 or the local file.

## Printing the template

1. Pick a design and set the board size and nail diameter.
2. Under **Export and share**, choose what to print on: one sheet (print shop or large printer), or A4 / A3 / Letter pages.
3. Download the **Print template – PDF** and print it at **100% / "Actual size"**, never "Fit to page".
4. Measure the 100 mm bar with a ruler before nailing.
5. **Tiled pages:** trim the right or lower page on its dashed line, lay it over its neighbour so the (+) marks match, and tape it. The small grid in each page corner shows where that page goes.
6. Nail through the crosshairs. The numbers are the same nail numbers the Android app and the plotter use.

## Sharing to WhatsApp or email

**Share** opens the phone's share menu with the file attached.

| File | Attaches directly on Android Chrome | Notes |
|---|---|---|
| PDF template | yes | best choice to send |
| PNG picture | yes | for customers |
| CSV nail list | yes | |
| SVG template / picture, ZIP of pages | usually no | |

When the browser can't attach a file (on most desktops, and for SVG and ZIP), the page downloads it instead. It then offers buttons that open WhatsApp or your email with a ready message, and you attach the downloaded file there.

## Files it makes

| Export | Use |
|---|---|
| `*-template.pdf` | real-size nail template; one page, or tiled pages in a single PDF |
| `*-template.svg` / `*-template-N-pages.zip` | the same as SVG (one sheet, or one SVG per page plus a how-to) |
| `*.png`, `*.svg` | coloured picture of the finished piece |
| `*-nails.csv` | `nail,x_mm,y_mm,ring` (origin top-left, Y down) |
| `*.mandala.json` | design file; **Open** loads it again |

## Machine check

The check uses a needle diameter (default 1.2 mm) and a clearance (default 0.3 mm). It warns about:
- nails too close for the needle to pass between;
- strings that run over another nail ("Show in red" marks them);
- nails without strings.

It is a quick screen. The Android app runs the exact check, with real wrap directions, before winding.

## Versions

The current version is shown in the page header ("What's new"). All changes to the web page, firmware and design core are in `../CHANGELOG.md` at the project root.

## Plotter jobs (step 3)

The route planning, collision checks and G-code are an exact port of `stringart-core`. Tested on 12 preset/option combinations, the G-code is identical, line by line and number by number, to what the Android app's core produces.

| Job | What happens |
|---|---|
| **Wind thread** | One G-code file covering every thread. The plotter stops for tie-on and tie-off, and strings that can't be wound are left out (shown dashed red in the Thread path view). |
| **Mark nails** | The pen or needle goes down once on every nail position, in a short travel order, so no paper template is needed. |

After changing this page, rebuild the firmware copy with `python firmware/tools/embed_web.py`.

## Simulation (step 3)

After **Generate job**, press **Simulate**. The page plays the exact G-code it will send: the needle, the thread as it is wound, the wrap loops, and every stop where the plotter waits for you to tie on or tie off. Choose a speed from ×1 to ×250 (the real job time and the playback time are both shown), and drag the slider to jump anywhere. **Send to plotter** appears once the simulation has been started.

## Themes

Pick one of seven themes from **Theme** in the header. The choice is kept in the browser and, when the dashboard is connected, on the plotter, so every phone or PC that opens the dashboard gets it again after a restart.
