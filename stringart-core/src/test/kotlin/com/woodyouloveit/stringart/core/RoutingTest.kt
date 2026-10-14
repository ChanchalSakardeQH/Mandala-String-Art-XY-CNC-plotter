package com.woodyouloveit.stringart.core

import org.junit.jupiter.api.Test
import kotlin.math.PI
import kotlin.math.abs
import kotlin.random.Random
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

class RoutingTest {
    private val spec = MachineSpec()

    private fun minimumRuns(d: Design): Int {
        val parent = IntArray(d.nails.size) { it }
        fun find(x: Int): Int { var r = x; while (parent[r] != r) r = parent[r]; return r }
        val deg = IntArray(d.nails.size)
        d.chords.forEach { deg[it.a]++; deg[it.b]++; parent[find(it.a)] = find(it.b) }
        val odd = HashMap<Int, Int>()
        for (v in d.nails.indices) if (deg[v] > 0) odd[find(v)] = (odd[find(v)] ?: 0) + deg[v] % 2
        return odd.values.sumOf { maxOf(1, it / 2) }
    }

    @Test
    fun everyStringIsWoundExactlyOnceWithMinimumRuns() {
        for ((name, cfg) in Presets.all) {
            val d = PatternGenerator.generate(cfg)
            val plan = RoutePlanner.plan(d, spec)
            val wound = plan.runs.flatMap { run -> run.steps.zipWithNext { a, b -> Chord.of(a.nail, b.nail) } }
            assertEquals(d.chords.size, wound.size, "$name: string count")
            assertEquals(d.chords.toSet(), wound.toSet(), "$name: strings differ")
            assertEquals(minimumRuns(d), plan.runs.size, "$name: run count")
        }
    }

    @Test
    fun closedLoopIsOneRun() {
        val b = DesignBuilder("square", 100.0, 100.0, "#fff")
        val p = listOf(Vec(20.0, 20.0), Vec(80.0, 20.0), Vec(80.0, 80.0), Vec(20.0, 80.0))
        for (i in 0 until 4) b.chord(p[i], p[(i + 1) % 4])
        val plan = RoutePlanner.plan(b.build(), spec)
        assertEquals(1, plan.runs.size)
        assertEquals(5, plan.runs[0].steps.size)
        assertEquals(plan.runs[0].steps.first().nail, plan.runs[0].steps.last().nail)
    }

    @Test
    fun singleRingWheelIsFullyWindable() {
        val d = PatternGenerator.generate(PatternConfig(pattern = Pattern.CLASSIC, rings = 1, nails = 120, step = 20))
        val r = StringArtPipeline.build(d, spec)
        assertTrue(r.jobReport.ok, r.jobReport.errors.toString())
        assertTrue(r.designReport.ok, r.designReport.errors.toString())
    }

    @Test
    fun connectorsReduceRuns() {
        val d = PatternGenerator.generate(Presets.all.getValue("Square Weave"))
        val without = StringArtPipeline.build(d, spec, BuildOptions(autoFix = true))
        val with = StringArtPipeline.build(d, spec, BuildOptions(autoFix = true, route = RouteOptions(connectorMaxLength = 10.0)))
        assertTrue(with.connectorsAdded.isNotEmpty())
        assertTrue(with.job.stats.runs < without.job.stats.runs, "${with.job.stats.runs} vs ${without.job.stats.runs}")
        assertTrue(with.jobReport.ok, with.jobReport.errors.toString())
    }

    @Test
    fun tangentsTouchBothCirclesAndFollowWrapDirection() {
        val rnd = Random(42)
        repeat(2000) {
            val a = Vec(rnd.nextDouble(0.0, 100.0), rnd.nextDouble(0.0, 100.0))
            val b = Vec(rnd.nextDouble(0.0, 100.0), rnd.nextDouble(0.0, 100.0))
            val r = rnd.nextDouble(0.5, 3.0)
            val sa = if (rnd.nextBoolean()) 1 else -1
            val sb = if (rnd.nextBoolean()) 1 else -1
            val t = Tangents.between(a, sa, b, sb, r) ?: return@repeat
            val (dep, arr) = t
            val line = arr - dep
            assertTrue(abs(dep.dist(a) - r) < 1e-9 && abs(arr.dist(b) - r) < 1e-9, "not on circles")
            assertTrue(abs((dep - a).dot(line)) < 1e-6 * line.length && abs((arr - b).dot(line)) < 1e-6 * line.length, "not tangent")
            // moving around a in direction sa at dep heads along the line; same for b at arr
            assertTrue((dep - a).rotate(sa * PI / 2).dot(line) > 0, "wrong side at departure")
            assertTrue((arr - b).rotate(sb * PI / 2).dot(line) > 0, "wrong side at arrival")
        }
        assertNull(Tangents.between(Vec(0.0, 0.0), 1, Vec(3.0, 0.0), -1, 2.0), "overlapping circles cannot cross")
    }

    @Test
    fun sweepAngles() {
        val c = Vec(0.0, 0.0)
        assertTrue(abs(Tangents.sweep(c, Vec(1.0, 0.0), Vec(0.0, 1.0), 1) - PI / 2) < 1e-9)
        assertTrue(abs(Tangents.sweep(c, Vec(1.0, 0.0), Vec(0.0, 1.0), -1) - 3 * PI / 2) < 1e-9)
        assertTrue(abs(Tangents.sweep(c, Vec(1.0, 0.0), Vec(1.0, 0.0), 1) - 2 * PI) < 1e-9)
    }
}
