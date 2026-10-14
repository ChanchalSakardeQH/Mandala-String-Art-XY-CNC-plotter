package com.woodyouloveit.stringart.core

import java.util.Locale
import kotlin.math.round

/**
 * Machine settings for G-code output.
 * The board's bottom-left corner sits at ([offsetX], [offsetY]) in machine coordinates.
 * Leave [zUp]/[zDown] null for a machine without a Z axis.
 */
data class GcodeConfig(
    val offsetX: Double = 0.0,
    val offsetY: Double = 0.0,
    val feed: Double = 3000.0,
    val rapidFeedForEstimate: Double = 6000.0,
    val zUp: Double? = null,
    val zDown: Double? = null,
    val zFeed: Double = 1000.0,
    /** Full turns around the first and last nail of each run to lock the thread. */
    val anchorTurns: Int = 2,
    /** Insert M0 so the operator can tie on / cut between runs. */
    val pauseBetweenRuns: Boolean = true,
)

data class GcodeResult(val text: String, val lineCount: Int, val pathLengthMm: Double, val estMinutes: Double)

/**
 * Writes a job as GRBL-compatible G-code (G21 G90 G17, G0/G1/G2/G3, M0, M2).
 * Every nail is preceded by a `; STEP run=R step=S nail=N` comment; to resume after a stop,
 * call [write] again with startRun/startStep and home the machine first.
 */
object GcodeWriter {

    fun write(job: Job, spec: MachineSpec, cfg: GcodeConfig = GcodeConfig(), startRun: Int = 0, startStep: Int = 0): GcodeResult {
        require(startRun in job.runs.indices) { "startRun out of range" }
        require(startStep in job.runs[startRun].steps.indices) { "startStep out of range" }
        val m = job.machineNails().map { Vec(it.x + cfg.offsetX, it.y + cfg.offsetY) }
        val r = spec.wrapRadius
        val out = StringBuilder()
        var lines = 0
        var cut = 0.0; var rapid = 0.0
        var pos: Vec? = null

        fun f(v: Double): String {
            val s = String.format(Locale.ROOT, "%.3f", v)
            return if (s == "-0.000") "0.000" else s
        }
        fun q(v: Vec) = Vec(round(v.x * 1000) / 1000, round(v.y * 1000) / 1000)
        fun emit(line: String) { out.append(line).append('\n'); lines++ }
        fun moveTo(p: Vec, rapidMove: Boolean) {
            val t = q(p)
            pos?.let { if (rapidMove) rapid += it.dist(t) else cut += it.dist(t) }
            emit(if (rapidMove) "G0 X${f(t.x)} Y${f(t.y)}" else "G1 X${f(t.x)} Y${f(t.y)} F${f(cfg.feed)}")
            pos = t
        }
        fun arc(center: Vec, to: Vec, dir: WrapDir) {
            val from = pos!!
            val t = q(to)
            val code = if (dir == WrapDir.CW) "G2" else "G3"
            // I/J from the rounded start so the controller's radius check (error 33) passes.
            emit("$code X${f(t.x)} Y${f(t.y)} I${f(center.x - from.x)} J${f(center.y - from.y)} F${f(cfg.feed)}")
            cut += Tangents.sweep(center, from, t, dir.sign) * r
            pos = t
        }
        fun fullTurns(center: Vec, dir: WrapDir, turns: Int) { repeat(turns) { arc(center, pos!!, dir) } }
        fun zUp() { cfg.zUp?.let { emit("G0 Z${f(it)}") } }
        fun zDown() { cfg.zDown?.let { emit("G1 Z${f(it)} F${f(cfg.zFeed)}") } }

        emit("; woodyouloveit.com - Mandala String Art")
        emit("; ${job.name.replace('\n', ' ')} - mandala string art job v${job.version}")
        emit("; runs=${job.stats.runs} wraps=${job.stats.wraps} thread=${f(job.stats.threadLengthM)} m")
        if (startRun > 0 || startStep > 0) emit("; RESUME from run=$startRun step=$startStep - home the machine before starting")
        emit("G21 G90 G17 G94")
        zUp()

        for (ri in startRun until job.runs.size) {
            val all = job.runs[ri].steps
            val first = if (ri == startRun) startStep else 0
            val steps = all.subList(first, all.size)
            if (steps.size < 2) { emit("; run $ri has nothing left to wind"); continue }
            emit("; RUN $ri color=${job.runs[ri].color} steps=${all.size}")

            val segs = (0 until steps.size - 1).map { i ->
                val a = steps[i]; val b = steps[i + 1]
                Tangents.between(m[a.nail], a.dir.sign, m[b.nail], b.dir.sign, r)
                    ?: throw IllegalStateException("Run $ri step ${first + i + 1}: nails ${a.nail} and ${b.nail} are too close for opposite wraps; run Validator.checkJob first")
            }

            emit("; STEP run=$ri step=$first nail=${steps[0].nail}")
            moveTo(segs[0].first, rapidMove = true)
            zDown()
            if (cfg.pauseBetweenRuns) emit("M0 ; tie thread on at nail ${steps[0].nail}")
            fullTurns(m[steps[0].nail], steps[0].dir, cfg.anchorTurns)

            for (i in 1 until steps.size) {
                val s = steps[i]
                emit("; STEP run=$ri step=${first + i} nail=${s.nail}")
                moveTo(segs[i - 1].second, rapidMove = false)
                if (i < steps.size - 1) {
                    // If departure == arrival after rounding, GRBL/FluidNC treat it as a full turn (matches Tangents.sweep).
                    arc(m[s.nail], segs[i].first, s.dir)
                } else {
                    fullTurns(m[s.nail], s.dir, cfg.anchorTurns)
                }
            }
            zUp()
            if (cfg.pauseBetweenRuns) emit("M0 ; tie off and cut at nail ${steps.last().nail}")
        }
        emit("M2")
        val minutes = cut / cfg.feed + rapid / cfg.rapidFeedForEstimate
        return GcodeResult(out.toString(), lines, cut + rapid, minutes)
    }
}
