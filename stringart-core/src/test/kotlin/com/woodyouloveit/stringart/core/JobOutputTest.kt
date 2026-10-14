package com.woodyouloveit.stringart.core

import org.junit.jupiter.api.Test
import kotlin.math.abs
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class JobOutputTest {
    private val spec = MachineSpec()

    private fun squareWeaveJob(): Job =
        StringArtPipeline.build(PatternGenerator.generate(Presets.all.getValue("Square Weave")), spec, BuildOptions(autoFix = true)).job

    @Test
    fun jsonRoundTrip() {
        val job = squareWeaveJob()
        val text = JobJson.write(job)
        val back = JobJson.read(text)
        assertEquals(text, JobJson.write(back))
        assertEquals(job.runs, back.runs)
        assertEquals(job.nails.size, back.nails.size)
        assertTrue(abs(job.stats.threadLengthM - back.stats.threadLengthM) < 0.01)
    }

    @Test
    fun jsonRejectsForeignFiles() {
        assertFailsWith<IllegalArgumentException> { JobJson.read("""{"format":"something-else","version":1}""") }
        assertFailsWith<IllegalArgumentException> { JobJson.read("""{"format":"woodyouloveit-stringart-job","version":2}""") }
    }

    private data class Arc(val from: Vec, val to: Vec, val center: Vec, val cw: Boolean)

    private fun parseArcs(gcode: String): List<Arc> {
        var x = 0.0; var y = 0.0
        val arcs = ArrayList<Arc>()
        for (raw in gcode.lines()) {
            val line = raw.substringBefore(';').trim()
            if (line.isEmpty()) continue
            val words = line.split(' ').associate { it[0] to it.substring(1) }
            val cmd = line.substringBefore(' ')
            val nx = words['X']?.toDouble() ?: x
            val ny = words['Y']?.toDouble() ?: y
            if (cmd == "G2" || cmd == "G3") {
                arcs.add(Arc(Vec(x, y), Vec(nx, ny), Vec(x + words.getValue('I').toDouble(), y + words.getValue('J').toDouble()), cmd == "G2"))
            }
            x = nx; y = ny
        }
        return arcs
    }

    @Test
    fun arcsPassControllerRadiusCheck() {
        val job = squareWeaveJob()
        val g = GcodeWriter.write(job, spec, GcodeConfig(offsetX = 10.0, offsetY = 20.0))
        val arcs = parseArcs(g.text)
        assertTrue(arcs.isNotEmpty())
        for (a in arcs) {
            val r0 = a.from.dist(a.center); val r1 = a.to.dist(a.center)
            assertTrue(abs(r0 - r1) < 0.003, "radius mismatch $a")
            assertTrue(abs(r0 - spec.wrapRadius) < 0.003, "wrong wrap radius $a")
        }
        assertEquals(job.stats.wraps, Regex("; STEP ").findAll(g.text).count())
        assertTrue(g.estMinutes > 0)
    }

    @Test
    fun yIsFlippedAndClockwiseIsG2() {
        // Board 100 mm tall, two nails 30 mm from the top edge -> machine Y = 70.
        val nails = listOf(Vec(20.0, 30.0), Vec(80.0, 30.0))
        val runs = listOf(Run("#fff", listOf(Step(0, WrapDir.CW), Step(1, WrapDir.CW))))
        val job = Job("t", 100.0, 100.0, 1.6, nails, runs, Job.computeStats(nails, 100.0, runs, 1.6))
        val arcs = parseArcs(GcodeWriter.write(job, spec).text)
        assertTrue(arcs.all { it.cw })
        assertTrue(arcs.all { abs(it.center.y - 70.0) < 1e-6 })
        // CW around both nails going +X: the thread runs along the top side (machine Y = 70 + r)
        assertTrue(abs(arcs.last().from.y - (70.0 + spec.wrapRadius)) < 1e-3)
    }

    @Test
    fun resumeStartsAtRequestedStep() {
        val job = squareWeaveJob()
        val run = job.runs.indices.maxBy { job.runs[it].steps.size }
        val g = GcodeWriter.write(job, spec, startRun = run, startStep = 5).text
        assertTrue(g.contains("; RESUME from run=$run step=5"))
        val first = Regex("; STEP run=(\\d+) step=(\\d+)").find(g)!!
        assertEquals("$run", first.groupValues[1]); assertEquals("5", first.groupValues[2])
    }

    @Test
    fun noZAxisMeansNoZWords() {
        val g = GcodeWriter.write(squareWeaveJob(), spec).text
        assertTrue(!g.contains(" Z"))
        val withZ = GcodeWriter.write(squareWeaveJob(), spec, GcodeConfig(zUp = 5.0, zDown = 0.0)).text
        assertTrue(withZ.contains("G0 Z5.000") && withZ.contains("G1 Z0.000"))
    }

    @Test
    fun validatorFindsBlockedStringsAndAutoFixRemovesThem() {
        // Three nails in a line: the 0-2 string runs straight over nail 1.
        val b = DesignBuilder("line", 100.0, 100.0, "#fff")
        val p0 = Vec(10.0, 50.0); val p1 = Vec(50.0, 50.0); val p2 = Vec(90.0, 50.0); val p3 = Vec(50.0, 10.0)
        b.chord(p0, p2); b.chord(p0, p3); b.chord(p3, p2); b.chord(p1, p3)
        val d = b.build()
        val raw = StringArtPipeline.build(d, spec)
        assertTrue(raw.jobReport.count("THREAD_HITS_NAIL") > 0)
        val fixed = StringArtPipeline.build(d, spec, BuildOptions(autoFix = true))
        assertTrue(fixed.jobReport.ok, fixed.jobReport.errors.toString())
        assertEquals(listOf(Chord.of(0, 1)), fixed.removedChords) // nails: 0=p0, 1=p2, 2=p3, 3=p1
    }

    @Test
    fun squareWeaveStringsAlongTheEdgesAreRemoved() {
        val r = StringArtPipeline.build(PatternGenerator.generate(Presets.all.getValue("Square Weave")), spec, BuildOptions(autoFix = true))
        assertEquals(12, r.removedChords.size)
        assertTrue(r.jobReport.ok)
        // the 4 corner-to-corner strings lie along a whole edge of nails; the other 8 graze a corner nail
        val corners = r.design.nails.withIndex().filter { (_, n) ->
            (abs(n.x - 30) < 1e-6 || abs(n.x - 270) < 1e-6) && (abs(n.y - 30) < 1e-6 || abs(n.y - 270) < 1e-6)
        }.map { it.index }.toSet()
        assertEquals(4, r.removedChords.count { it.a in corners && it.b in corners })
    }

    @Test
    fun heartWebNailsAreTooCloseToBuild() {
        val r = StringArtPipeline.build(PatternGenerator.generate(Presets.all.getValue("Heart Web")), spec)
        assertTrue(r.designReport.count("NAILS_TOO_CLOSE") > 0)
    }

    @Test
    fun specSanity() {
        val d = PatternGenerator.generate(PatternConfig(pattern = Pattern.CLASSIC, rings = 1))
        val bad = Validator.checkDesign(d, MachineSpec(wrapRadius = 1.0))
        assertTrue(bad.count("SPEC_WRAP_RADIUS") == 1)
        val small = Validator.checkDesign(d, MachineSpec(workWidth = 200.0, workHeight = 200.0))
        assertTrue(small.count("BOARD_TOO_WIDE") == 1 && small.count("BOARD_TOO_TALL") == 1)
    }
}
