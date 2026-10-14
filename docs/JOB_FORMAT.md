# String Art Job Format — v1

The Job is the contract between the design core, the Android app and the machine. It describes **what to wind**, not **how a particular machine moves**. Machine details (needle size, wrap radius, feed, board offset) live in `MachineSpec` / `GcodeConfig` and are applied when G-code is generated.

## Example

```json
{
  "format": "woodyouloveit-stringart-job",
  "version": 1,
  "name": "Square Weave",
  "units": "mm",
  "board": { "width": 300, "height": 300, "yAxis": "down" },
  "nail": { "diameter": 1.6 },
  "nails": [
    [30, 30], [270, 30], [38.276, 30]
  ],
  "runs": [
    { "color": "#f2b134", "steps": [
      [112, "cw"], [51, "cw"], [50, "ccw"]
    ] }
  ],
  "stats": { "runs": 13, "wraps": 219, "chords": 206, "threadLengthM": 34.151 }
}
```

A complete file is in `samples/square-weave.job.json`.

## Fields

| Field | Meaning |
|---|---|
| `format` | Always `woodyouloveit-stringart-job`. Readers reject anything else. |
| `version` | `1`. Readers reject versions they don't know. |
| `name` | Display name. |
| `units` | Always `mm`. |
| `board.width`, `board.height` | Board size. |
| `board.yAxis` | Always `down`: origin is the **top-left** corner, Y grows downward (same as SVG and the screen). |
| `nail.diameter` | Nail shank diameter. |
| `nails` | `[x, y]` centres, 3 decimals. A nail's **index in this array is its ID** everywhere else. |
| `runs` | Separate pieces of thread, wound in order. |
| `runs[].color` | Thread colour, `#rrggbb`. |
| `runs[].steps` | `[nailIndex, "cw" \| "ccw"]` in winding order. |
| `stats` | Summary for display. Readers **recompute** it from the steps rather than trusting it. |

## Rules

**Wrap direction.** `cw` and `ccw` are as seen looking at the front of the board. On an XY machine whose Y axis points up (away from the operator), `cw` is G2 and `ccw` is G3. The flip from `yAxis: down` to machine coordinates (`machineY = boardHeight − y`) happens only in the G-code writer. Flipping in two places mirrors the design.

**Runs.**
- The thread is tied on at the first step and tied off at the last.
- Between two consecutive steps there is one straight string.
- A run whose first and last nail are the same is a closed loop.
- Every string in the design appears exactly once across all runs.

**Connectors.** Strings added only to reduce the number of runs are ordinary steps. They are visible on the finished piece.

**Compatibility.** v1 readers ignore fields they don't know. Additions keep version 1; anything that changes the meaning of an existing field becomes version 2.

## Changes from the first draft

- `wrapRadius` moved out of the Job into `MachineSpec`, because it belongs to the machine's needle, not to the design.
- The time estimate moved to the G-code result, because it depends on the machine's feed rate.
