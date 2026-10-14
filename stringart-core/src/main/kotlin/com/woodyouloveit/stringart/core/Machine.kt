package com.woodyouloveit.stringart.core

import kotlin.math.PI
import kotlin.math.asin
import kotlin.math.ceil
import kotlin.math.max

/**
 * Physical parameters of the board and the wrapping head. Lengths in mm.
 * @param wrapRadius distance from nail centre to needle centre while wrapping.
 * @param clearance extra gap kept between needle/thread and any nail.
 * @param workWidth, workHeight usable XY travel; null skips the fit check.
 */
data class MachineSpec(
    val nailDiameter: Double = 1.6,
    val needleDiameter: Double = 1.2,
    val wrapRadius: Double = 2.0,
    val clearance: Double = 0.3,
    val workWidth: Double? = null,
    val workHeight: Double? = null,
) {
    val nailR get() = nailDiameter / 2
    val needleR get() = needleDiameter / 2
    /** Minimum distance from a nail centre to the needle centre line. */
    val needleClear get() = nailR + needleR + clearance
    /** Minimum distance from a nail centre to the thread line. */
    val threadClear get() = nailR + clearance
}

enum class WrapDir(val sign: Int, val json: String) {
    /** Clockwise as seen from the front of the board (G2 on the machine). */
    CW(-1, "cw"),
    /** Counter-clockwise as seen from the front of the board (G3 on the machine). */
    CCW(1, "ccw");

    companion object {
        fun ofSign(s: Int) = if (s > 0) CCW else CW
        fun ofJson(s: String) = entries.first { it.json == s }
    }
}

/** Tangent lines between wrap circles. All maths in the machine frame (Y up). */
object Tangents {
    /**
     * Line leaving the circle around [a] (moving in direction [sa]) and arriving on the circle around [b]
     * (which will then be wrapped in direction [sb]). Returns (departure point, arrival point),
     * or null if the crossing tangent is impossible because the circles overlap.
     */
    fun between(a: Vec, sa: Int, b: Vec, sb: Int, r: Double): Pair<Vec, Vec>? {
        val d = b - a
        val dist = d.length
        if (dist < 1e-9) return null
        val u = d / dist
        val phi = if (sa == sb) 0.0 else {
            val k = 2 * r / dist
            if (k >= 1.0) return null
            sa * asin(k)
        }
        val t = u.rotate(phi)
        val na = t.rotate(-sa * PI / 2)
        val nb = t.rotate(-sb * PI / 2)
        return Pair(a + na * r, b + nb * r)
    }

    /** Angle swept going from [from] to [to] around [c] in direction [s]. Equal points mean a full turn. */
    fun sweep(c: Vec, from: Vec, to: Vec, s: Int): Double {
        val a0 = (from - c).angle(); val a1 = (to - c).angle()
        val d = if (s > 0) Geo.norm(a1 - a0) else Geo.norm(a0 - a1)
        return if (d < 1e-9) Geo.TWO_PI else d
    }
}

/** Finds nails that a line or an arc would hit. Nails are given in the machine frame. */
class CollisionProbe(private val nails: List<Vec>, private val spec: MachineSpec) {
    private val near: Array<IntArray>

    init {
        // Nails close enough to be touched by the needle while wrapping a given nail.
        val reach = spec.wrapRadius + spec.needleClear
        near = Array(nails.size) { i ->
            val out = ArrayList<Int>()
            for (j in nails.indices) if (j != i && nails[i].dist(nails[j]) < reach) out.add(j)
            out.toIntArray()
        }
    }

    /** Nails (other than [skipA], [skipB]) closer than [limit] to segment p-q. */
    fun lineHits(p: Vec, q: Vec, limit: Double, skipA: Int, skipB: Int, out: MutableList<Int>? = null): Int {
        val minX = minOf(p.x, q.x) - limit; val maxX = maxOf(p.x, q.x) + limit
        val minY = minOf(p.y, q.y) - limit; val maxY = maxOf(p.y, q.y) + limit
        var hits = 0
        for (k in nails.indices) {
            if (k == skipA || k == skipB) continue
            val n = nails[k]
            if (n.x < minX || n.x > maxX || n.y < minY || n.y > maxY) continue
            if (Geo.distToSegment(n, p, q) < limit) { hits++; out?.add(k) }
        }
        return hits
    }

    /** Nails the needle touches while sweeping from [from] to [to] around nail [i] in direction [s]. */
    fun arcHits(i: Int, from: Vec, to: Vec, s: Int, out: MutableList<Int>? = null): Int {
        if (near[i].isEmpty()) return 0
        val c = nails[i]
        val sweep = Tangents.sweep(c, from, to, s)
        val start = (from - c).angle()
        val steps = max(4, ceil(sweep * spec.wrapRadius / 0.25).toInt())
        var hits = 0
        for (k in near[i]) {
            for (j in 0..steps) {
                val a = start + s * sweep * j / steps
                val pt = Geo.polar(c.x, c.y, spec.wrapRadius, a)
                if (pt.dist(nails[k]) < spec.needleClear) { hits++; out?.add(k); break }
            }
        }
        return hits
    }
}
