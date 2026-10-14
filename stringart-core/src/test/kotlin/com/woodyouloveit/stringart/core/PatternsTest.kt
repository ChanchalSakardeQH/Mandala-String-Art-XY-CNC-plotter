package com.woodyouloveit.stringart.core

import org.junit.jupiter.api.Test
import kotlin.math.abs
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class PatternsTest {

    /**
     * nails / strings per preset. String counts equal the web tool V1.2 segment counts minus the
     * duplicates it drew twice, so they double as a parity check with the TypeScript engine.
     */
    private val golden = mapOf(
        "Vertical Diamond" to (274 to 315), "Hex Flower" to (276 to 396), "Layered Star" to (228 to 342),
        "Triangle Portal" to (360 to 270), "Square Weave" to (116 to 172), "Heart Web" to (293 to 269),
        "Orange Petal" to (540 to 720), "Blue Weave" to (288 to 504), "Purple Lace" to (720 to 1368),
        "Red Crown" to (480 to 660), "Rainbow Galaxy" to (840 to 1428), "Lotus Bloom" to (528 to 1200),
        "Golden Star" to (360 to 480), "Teal Vortex" to (864 to 1224),
    )

    @Test
    fun presetsMatchGoldenCounts() {
        assertEquals(golden.keys, Presets.all.keys)
        for ((name, cfg) in Presets.all) {
            val d = PatternGenerator.generate(cfg)
            assertEquals(golden[name], d.nails.size to d.chords.size, name)
        }
    }

    @Test
    fun everyStringEndsOnARealNail() {
        for ((name, cfg) in Presets.all) {
            val d = PatternGenerator.generate(cfg)
            for (c in d.chords) {
                assertTrue(c.a < c.b && c.b < d.nails.size, "$name: bad chord $c")
            }
            assertEquals(d.chords.size, d.chords.toSet().size, "$name: duplicate strings")
        }
    }

    @Test
    fun noDuplicateNails() {
        for ((name, cfg) in Presets.all) {
            val nails = PatternGenerator.generate(cfg).nails.sortedBy { it.x }
            for (i in nails.indices) {
                var j = i + 1
                while (j < nails.size && nails[j].x - nails[i].x <= 0.01) {
                    assertTrue(nails[i].pos.dist(nails[j].pos) > 0.01, "$name: duplicate nail near ${nails[i]}")
                    j++
                }
            }
        }
    }

    @Test
    fun structuredPatternsScaleWithWidth() {
        for (p in Pattern.entries.filter { it.structured }) {
            val big = PatternGenerator.generate(PatternConfig(pattern = p, diameter = 300.0, frame = Frame.PORTRAIT))
            val small = PatternGenerator.generate(PatternConfig(pattern = p, diameter = 150.0, frame = Frame.PORTRAIT))
            assertEquals(big.nails.size, small.nails.size, "$p nail count")
            big.nails.zip(small.nails).forEach { (a, b) ->
                assertTrue(abs(a.x / 2 - b.x) < 1e-6 && abs(a.y / 2 - b.y) < 1e-6, "$p does not scale")
            }
        }
    }

    @Test
    fun extremeSettingsDoNotCrash() {
        for (p in Pattern.entries) {
            val d = PatternGenerator.generate(PatternConfig(pattern = p, nails = 24, step = 40, rings = 8, ringGap = 45.0, moduleCount = 6, innerWeave = 4))
            assertTrue(d.nails.isNotEmpty(), "$p produced no nails")
        }
        // all rings filtered out (tiny board): empty but valid design
        val empty = PatternGenerator.generate(PatternConfig(pattern = Pattern.LOTUS, diameter = 12.0))
        assertEquals(0, empty.chords.size)
    }
}
