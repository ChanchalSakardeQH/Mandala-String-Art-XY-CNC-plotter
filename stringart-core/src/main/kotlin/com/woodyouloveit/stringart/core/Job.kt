package com.woodyouloveit.stringart.core

/** One wrap: go to [nail] and wrap it in [dir]. */
data class Step(val nail: Int, val dir: WrapDir)

/** One continuous piece of thread: tie on at the first step, tie off at the last. */
data class Run(val color: String, val steps: List<Step>)

data class JobStats(
    val runs: Int,
    val wraps: Int,
    val chords: Int,
    /** Thread on the board (strings + wraps), excluding tie-off tails. */
    val threadLengthM: Double,
)

/**
 * A machine-independent string art job. Nails use the preview frame (mm, origin top-left, Y down).
 * Wrap directions are as seen from the front of the board.
 */
data class Job(
    val name: String,
    val width: Double,
    val height: Double,
    val nailDiameter: Double,
    val nails: List<Vec>,
    val runs: List<Run>,
    val stats: JobStats,
    val version: Int = FORMAT_VERSION,
) {
    fun machineNails(): List<Vec> = nails.map { Design.toMachine(it, height) }

    companion object {
        const val FORMAT = "woodyouloveit-stringart-job"
        const val FORMAT_VERSION = 1

        fun computeStats(nails: List<Vec>, height: Double, runs: List<Run>, nailDiameter: Double): JobStats {
            val m = nails.map { Design.toMachine(it, height) }
            val r = nailDiameter / 2
            var length = 0.0
            for (run in runs) {
                val st = run.steps
                var arrival: Vec? = null
                for (i in 0 until st.size - 1) {
                    val a = st[i]; val b = st[i + 1]
                    val t = Tangents.between(m[a.nail], a.dir.sign, m[b.nail], b.dir.sign, r)
                    if (t == null) { length += m[a.nail].dist(m[b.nail]); arrival = null; continue }
                    val (dep, arr) = t
                    // wrap at nail a: from where we arrived to where we leave (first nail: one full turn)
                    length += if (arrival == null) Geo.TWO_PI * r
                    else Tangents.sweep(m[a.nail], arrival, dep, a.dir.sign) * r
                    length += dep.dist(arr)
                    arrival = arr
                }
                if (st.size >= 2) length += Geo.TWO_PI * r // tie-off turn on the last nail
            }
            return JobStats(
                runs = runs.size,
                wraps = runs.sumOf { it.steps.size },
                chords = runs.sumOf { maxOf(0, it.steps.size - 1) },
                threadLengthM = length / 1000.0,
            )
        }
    }
}
