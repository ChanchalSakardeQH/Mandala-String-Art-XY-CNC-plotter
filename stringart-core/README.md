# stringart-core

Design engine for the Mandala String Art machine. It is plain Kotlin with no Android or third-party dependencies. The Android app uses it, and so can any JVM tool.

```
PatternConfig ──► PatternGenerator ──► Design (indexed nails + strings)
                                          │
                       StringArtPipeline.build(design, MachineSpec, BuildOptions)
                                          │  route planning + collision checks (+ optional auto-fix)
                                          ▼
                                    Job ──► JobJson (save / send)
                                       └──► GcodeWriter ──► FluidNC / GRBL
```

## Add it to the Android project

1. Don't copy the folder. The Android project in `../android` points at it, so the app and the core always build from the same code.
2. In `android/settings.gradle.kts`:
   ```kotlin
   include(":stringart-core")
   project(":stringart-core").projectDir = file("../stringart-core")
   ```
3. In the root `build.gradle.kts` `plugins { }` block, add the JVM Kotlin plugin at the **same version** as your Android Kotlin plugin:
   ```kotlin
   id("org.jetbrains.kotlin.jvm") version "<your Kotlin version>" apply false
   ```
   If you use a version catalog, add a `kotlin-jvm` plugin alias that points at the same `kotlin` version.
4. In `app/build.gradle.kts`:
   ```kotlin
   dependencies { implementation(project(":stringart-core")) }
   ```
5. Run the tests: `gradlew :stringart-core:test`. They run on the PC; no emulator is needed.

You need Kotlin 2.0 or later for the `compilerOptions` syntax in `build.gradle.kts`.

## Use it

```kotlin
val design = PatternGenerator.generate(Presets.all.getValue("Square Weave"))

// Measure these on the real machine; the defaults are placeholders.
val spec = MachineSpec(
    nailDiameter = 1.6, needleDiameter = 1.2, wrapRadius = 2.0, clearance = 0.3,
    workWidth = 400.0, workHeight = 400.0,
)

val result = StringArtPipeline.build(
    design, spec,
    BuildOptions(autoFix = true, route = RouteOptions(connectorMaxLength = 10.0)),
)

// result.designReport / result.jobReport: issues with severity, code, count and examples ("run:step" or nail ids)
// result.removedChords: strings auto-fix dropped (show them to the user!)
// result.connectorsAdded: extra visible strings added to reduce tie-offs

val json  = JobJson.write(result.job)
val gcode = GcodeWriter.write(result.job, spec, GcodeConfig(offsetX = 10.0, offsetY = 10.0)).text
```

Call `build()` off the main thread (for example `Dispatchers.Default`). The largest preset takes about 0.5 s on a desktop JVM, and a phone will be slower.

**Resume after a stop.** Every nail in the G-code starts with a `; STEP run=R step=S nail=N` comment. Track the last one the controller reached, home the machine, then call `GcodeWriter.write(job, spec, cfg, startRun = R, startStep = S)`.

## What the pipeline does

- **Generator.** A port of web tool V1.2. Every string ends on a real nail, shared points become a single nail, duplicate strings are merged, and margins scale with board width. String counts match the web tool exactly once its duplicates are removed (this is checked in `PatternsTest`).
- **Route planner.** Each connected group of strings with *k* odd-degree nails needs max(1, *k*/2) separate threads. The planner reaches that minimum using Euler circuits. Optional connectors join nearby odd nails to cut the number of tie-offs.
- **Wrap directions.** Chosen per nail by dynamic programming so that the needle path, the thread path and the wrap arc avoid other nails, with the shortest total wrap as a tie-break.
- **Validator.** Design-level checks: nail spacing, edge distance, board fits the work area, spec sanity, unused nails. Job-level checks: the exact needle and thread lines and every wrap arc.
- **G-code.** Uses only G0/G1/G2/G3, M0 and M2. Each arc's I/J is computed from the rounded start point so the controller's radius check passes. The first and last nail of each run get anchor turns. Z moves are optional.

## Known limits

- **Multi-ring presets lose many strings to auto-fix.** Their strings cross the nails of inner rings; for example, Purple Lace drops 833 of 1368. Those patterns need redesigning for winding, not just checking.
- Wrap directions don't yet consider thread already on the board (stacking order and tension).
- The G-code has been checked for format, arc consistency and coordinates in tests, but not yet on hardware. Try the first run without thread at a low feed.
- `MachineSpec` defaults are placeholders until they are measured on your needle head.

## Files

| File | Contents |
|---|---|
| `Geometry.kt` | `Vec`, polygon / edge helpers |
| `Design.kt` | `Nail`, `Chord`, `Design`, `DesignBuilder` (snaps points to nails) |
| `Patterns.kt` | `Pattern`, `Frame`, `PatternConfig`, `Presets`, `PatternGenerator` |
| `Machine.kt` | `MachineSpec`, `WrapDir`, tangent maths, `CollisionProbe` |
| `RoutePlanner.kt` | runs + wrap directions |
| `Validation.kt` | `Validator`, `Report`, `StringArtPipeline` |
| `Job.kt`, `JobJson.kt` | Job model and JSON (see `../docs/JOB_FORMAT.md`) |
| `GcodeWriter.kt` | G-code output with resume |
| `samples/` | Square Weave job and its G-code |
