package com.woodyouloveit.stringart.core

import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.sin

enum class Pattern(val label: String, val structured: Boolean) {
    CLASSIC("Classic Wheel", false),
    SPIRAL("Spiral Flow", false),
    STAR("Starburst", false),
    PETAL("Petal Bloom", false),
    LACE("Lace Flower", false),
    CROWN("Sharp Crown", false),
    WEAVE("Woven Mandala", false),
    VORTEX("Vortex", false),
    LOTUS("Lotus", false),
    GALAXY("Galaxy Web", false),
    VERTICAL_DIAMOND("Vertical Diamond", true),
    HEX_FLOWER("Hex Flower", true),
    LAYERED_STAR("Layered Star", true),
    TRIANGLE_PORTAL("Triangle Portal", true),
    SQUARE_WEAVE("Square Weave", true),
    HEART_WEB("Heart Web", true),
}

/** Board shape. Ring patterns are always a circle on a square board; Portrait makes structured boards 1.42× tall. */
enum class Frame { CIRCLE, SQUARE, PORTRAIT }

/** Same parameters as the web tool's Config (geometry-relevant fields only). Lengths in mm. */
data class PatternConfig(
    val diameter: Double = 300.0,
    val nails: Int = 120,
    val rings: Int = 4,
    val ringGap: Double = 18.0,
    val step: Int = 7,
    val rotationDeg: Double = 0.0,
    val pattern: Pattern = Pattern.VERTICAL_DIAMOND,
    val frame: Frame = Frame.PORTRAIT,
    val innerWeave: Int = 2,
    val moduleCount: Int = 3,
    val nailSize: Double = 1.6,
    val stringColor: String = "#12c7d8",
)

object Presets {
    private val d = PatternConfig()

    /** The 14 reference designs from the web tool V1.2, applied over the defaults. */
    val all: Map<String, PatternConfig> = linkedMapOf(
        "Vertical Diamond" to d.copy(frame = Frame.PORTRAIT, pattern = Pattern.VERTICAL_DIAMOND, nails = 96, rings = 4, ringGap = 18.0, step = 8, moduleCount = 3, stringColor = "#12c7d8", innerWeave = 2),
        "Hex Flower" to d.copy(frame = Frame.SQUARE, pattern = Pattern.HEX_FLOWER, nails = 144, rings = 4, ringGap = 16.0, step = 9, moduleCount = 6, stringColor = "#70d34b", innerWeave = 2),
        "Layered Star" to d.copy(frame = Frame.SQUARE, pattern = Pattern.LAYERED_STAR, nails = 120, rings = 5, ringGap = 13.0, step = 11, moduleCount = 3, stringColor = "#2fd0d7", innerWeave = 2),
        "Triangle Portal" to d.copy(frame = Frame.CIRCLE, pattern = Pattern.TRIANGLE_PORTAL, nails = 180, rings = 3, ringGap = 18.0, step = 12, moduleCount = 3, stringColor = "#f1f1ea", innerWeave = 2),
        "Square Weave" to d.copy(frame = Frame.SQUARE, pattern = Pattern.SQUARE_WEAVE, nails = 120, rings = 3, ringGap = 20.0, step = 7, moduleCount = 4, stringColor = "#f2b134", innerWeave = 2),
        "Heart Web" to d.copy(frame = Frame.CIRCLE, pattern = Pattern.HEART_WEB, nails = 180, rings = 3, ringGap = 14.0, step = 9, moduleCount = 1, stringColor = "#f5f5f5", innerWeave = 2),
        "Orange Petal" to d.copy(nails = 120, rings = 4, ringGap = 17.0, step = 7, pattern = Pattern.PETAL, stringColor = "#d97706", innerWeave = 1),
        "Blue Weave" to d.copy(nails = 72, rings = 4, ringGap = 25.0, step = 11, pattern = Pattern.WEAVE, stringColor = "#0891b2", innerWeave = 2),
        "Purple Lace" to d.copy(nails = 144, rings = 5, ringGap = 14.0, step = 9, pattern = Pattern.LACE, stringColor = "#9333ea", innerWeave = 2),
        "Red Crown" to d.copy(nails = 120, rings = 4, ringGap = 19.0, step = 8, pattern = Pattern.CROWN, stringColor = "#b91c1c", innerWeave = 1),
        "Rainbow Galaxy" to d.copy(nails = 168, rings = 5, ringGap = 13.0, step = 13, pattern = Pattern.GALAXY, stringColor = "#7c3aed", innerWeave = 3),
        "Lotus Bloom" to d.copy(nails = 96, rings = 5, ringGap = 16.0, step = 6, pattern = Pattern.LOTUS, stringColor = "#db2777", innerWeave = 1),
        "Golden Star" to d.copy(nails = 120, rings = 3, ringGap = 25.0, step = 17, pattern = Pattern.STAR, stringColor = "#eab308", innerWeave = 1),
        "Teal Vortex" to d.copy(nails = 144, rings = 6, ringGap = 11.0, step = 5, pattern = Pattern.VORTEX, stringColor = "#0f766e", innerWeave = 2),
    )
}

/**
 * Port of the web tool's geometry engine (V1.2) with these fixes:
 *  - every string endpoint is a real nail (structured patterns attached strings to points that were never nails);
 *  - shared corners are one nail, duplicate strings are merged;
 *  - structured-pattern margins scale with width (identical to the web tool at 300 mm);
 *  - negative modulo in the crown inter-ring links can no longer index outside the ring.
 */
object PatternGenerator {

    fun generate(c: PatternConfig): Design {
        require(c.nails >= 3) { "nails must be at least 3" }
        return if (c.pattern.structured) structured(c) else rings(c)
    }

    private fun mod(a: Int, n: Int) = Math.floorMod(a, n)

    private fun rings(c: PatternConfig): Design {
        val w = c.diameter
        val cx = w / 2; val cy = w / 2
        val maxR = w / 2 - c.nailSize * 2
        val radii = (0 until c.rings).map { maxR - it * c.ringGap }.filter { it > 5 }
        val n = c.nails
        val rot = c.rotationDeg * PI / 180
        val b = DesignBuilder(c.pattern.label, w, w, c.stringColor)
        val pts = radii.map { r -> List(n) { i -> Geo.polar(cx, cy, r, i.toDouble() / n * Geo.TWO_PI + rot) } }
        pts.forEachIndexed { ri, ring -> ring.forEach { b.nail(it, ri) } }

        val waveDiv = max(1, Math.round(n / 24.0).toInt())
        for (ri in radii.indices) {
            val p = pts[ri]
            val s = c.step + if (ri % 2 == 1) c.step / 2 else 0
            for (i in 0 until n) {
                val wave = i / waveDiv
                val t = when (c.pattern) {
                    Pattern.SPIRAL -> i + s + ri * 3
                    Pattern.STAR -> i + if (i % 2 == 1) s else s * 2 + ri
                    Pattern.PETAL -> i + s + if (ri % 2 == 1) n / 2 else 0
                    Pattern.LACE -> i + s + if (i % 3 == 0) s * 2 else 0
                    Pattern.CROWN -> i + if (i % 2 == 1) s else -s
                    Pattern.WEAVE -> i + if (i % 2 == 1) s else -s * 2
                    Pattern.VORTEX -> i + s + i / 12 + ri
                    Pattern.LOTUS -> i + s + if (ri % 3 == 0) n / 3 else 0
                    Pattern.GALAXY -> i + s + wave * (if (ri % 2 == 1) 1 else -1) + ri * 2
                    else -> i + s
                }
                b.chord(p[i], p[mod(t, n)], ri)
                val weaves = c.pattern == Pattern.LACE || c.pattern == Pattern.LOTUS || c.pattern == Pattern.GALAXY
                if (c.innerWeave > 0 && weaves && i % c.innerWeave == 0) {
                    val t2 = mod(i - s * (if (c.pattern == Pattern.GALAXY) 1 else 2), n)
                    b.chord(p[i], p[t2], ri)
                }
            }
        }
        for (ri in 0 until radii.size - 1) {
            val offset = c.step + ri * 2
            var i = 0
            while (i < n) {
                val target = if (c.pattern == Pattern.CROWN && ri % 2 == 1) mod(i - offset, n) else mod(i + offset, n)
                b.chord(pts[ri][i], pts[ri + 1][target], ri, ri + 1)
                i += if (c.pattern == Pattern.WEAVE) 1 else 2
            }
        }
        if ((c.pattern == Pattern.LOTUS || c.pattern == Pattern.PETAL) && radii.isNotEmpty()) {
            val inner = pts.last()
            val rc = max(3.0, radii.last() * 0.22)
            var i = 0
            while (i < n) {
                b.chord(inner[i], Geo.polar(cx, cy, rc, i.toDouble() / n * Geo.TWO_PI + rot), radii.size - 1, radii.size)
                i += 2
            }
        }
        return b.build()
    }

    private fun structured(c: PatternConfig): Design {
        val w = c.diameter
        val h = if (c.frame == Frame.PORTRAIT) w * 1.42 else w
        val cx = w / 2; val cy = h / 2
        val sc = w / 300.0 // margins were hard-coded mm at 300 mm width
        val n = max(24, c.nails)
        val b = DesignBuilder(c.pattern.label, w, h, c.stringColor)
        fun chain(a: List<Vec>, bb: List<Vec>, reverse: Boolean = false) {
            val count = minOf(a.size, bb.size)
            for (i in 0 until count) b.chord(a[i], bb[if (reverse) count - 1 - i else i])
        }

        when (c.pattern) {
            Pattern.VERTICAL_DIAMOND -> {
                val modules = max(1, c.moduleCount)
                val moduleH = (h - 60 * sc) / modules
                val count = max(18, n / 4)
                for (m in 0 until modules) {
                    val y0 = 30 * sc + m * moduleH; val y1 = y0 + moduleH
                    val tl = Vec(cx, y0); val tr = Vec(w - 35 * sc, y0 + moduleH * .42)
                    val br = Vec(cx, y1); val bl = Vec(35 * sc, y0 + moduleH * .42)
                    val a = Geo.edgePoints(tl, bl, count); val bb = Geo.edgePoints(tl, tr, count)
                    val cc = Geo.edgePoints(br, tr, count); val dd = Geo.edgePoints(br, bl, count)
                    chain(a, dd); chain(bb, cc); chain(a, cc, true); chain(bb, dd, true)
                    var i = 0
                    while (i < count) { b.chord(a[i], bb[(i + c.step) % count]); i += 2 }
                }
            }
            Pattern.HEX_FLOWER -> {
                val hex = Geo.regularPolygon(cx, cy, w * .43, 6)
                val count = max(18, n / 6)
                for (s in 0 until 6) {
                    val a = hex[s]; val bb = hex[(s + 1) % 6]; val prev = hex[(s + 5) % 6]
                    val side = Geo.edgePoints(a, bb, count); val other = Geo.edgePoints(a, prev, count, true)
                    chain(side, other, true)
                    chain(side, Geo.edgePoints(bb, prev, count))
                    var i = 0
                    while (i < count) { b.chord(side[i], other[(i + c.step) % count]); i += 2 }
                }
                for (s in 0 until 6) {
                    val p = Geo.polar(cx, cy, w * .17, -PI / 2 + s * PI / 3)
                    val outer = Geo.edgePoints(hex[s], hex[(s + 1) % 6], count)
                    var i = 0
                    while (i < outer.size) { b.chord(p, outer[i]); i += 3 }
                }
            }
            Pattern.LAYERED_STAR -> {
                val outer = Geo.regularPolygon(cx, cy, w * .43, 6)
                val inner = Geo.regularPolygon(cx, cy, w * .21, 6, -PI / 2 + PI / 6)
                val count = max(20, n / 6)
                for (s in 0 until 6) {
                    val o1 = Geo.edgePoints(outer[s], outer[(s + 1) % 6], count)
                    val i1 = Geo.edgePoints(inner[s], inner[(s + 1) % 6], count)
                    chain(o1, i1); chain(o1, i1, true)
                    chain(Geo.edgePoints(outer[(s + 5) % 6], outer[s], count), i1)
                }
            }
            Pattern.TRIANGLE_PORTAL -> {
                val outer = Geo.regularPolygon(cx, cy, w * .45, n)
                val tri = Geo.regularPolygon(cx, cy, w * .18, 3)
                for (i in outer.indices) {
                    val f = i.toDouble() / outer.size * 3
                    val sector = f.toInt(); val next = (sector + 1) % 3
                    b.chord(outer[i], Geo.lerp(tri[sector], tri[next], f - sector))
                    if (i % max(1, c.innerWeave) == 0) b.chord(outer[i], tri[(sector + 2) % 3])
                }
            }
            Pattern.SQUARE_WEAVE -> {
                val m = 30 * sc
                val count = max(20, n / 4)
                val corners = listOf(Vec(m, m), Vec(w - m, m), Vec(w - m, h - m), Vec(m, h - m))
                val sides = corners.mapIndexed { i, p -> Geo.edgePoints(p, corners[(i + 1) % 4], count) }
                for (s in 0 until 4) {
                    chain(sides[s], sides[(s + 1) % 4])
                    chain(sides[s], sides[(s + 2) % 4], true)
                }
            }
            Pattern.HEART_WEB -> {
                val outer = Geo.regularPolygon(cx, cy, w * .45, n)
                val heart = List(180) { i ->
                    val t = i / 180.0 * Geo.TWO_PI
                    val x = 16 * sin(t).pow(3)
                    val y = -(13 * cos(t) - 5 * cos(2 * t) - 2 * cos(3 * t) - cos(4 * t))
                    Vec(cx + x * w / 42, cy + y * w / 42)
                }
                for (i in outer.indices) {
                    b.chord(outer[i], heart[(i * 7 + (i / 9) * c.step) % heart.size])
                    if (i % 2 == 0) b.chord(outer[i], heart[(i * 11 + 35) % heart.size])
                }
            }
            else -> error("${c.pattern} is not a structured pattern")
        }
        return b.build()
    }
}
