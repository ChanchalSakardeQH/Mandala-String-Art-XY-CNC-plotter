package com.woodyouloveit.stringart.core

import kotlin.math.PI
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.sqrt
import kotlin.math.sin

/** 2D point / vector in millimetres. */
data class Vec(val x: Double, val y: Double) {
    operator fun plus(o: Vec) = Vec(x + o.x, y + o.y)
    operator fun minus(o: Vec) = Vec(x - o.x, y - o.y)
    operator fun times(k: Double) = Vec(x * k, y * k)
    operator fun div(k: Double) = Vec(x / k, y / k)
    fun dot(o: Vec) = x * o.x + y * o.y
    fun cross(o: Vec) = x * o.y - y * o.x
    // sqrt of squares (not hypot): bit-identical on JVM, Android and JavaScript, so planners agree on ties.
    val length: Double get() = sqrt(x * x + y * y)
    fun dist(o: Vec): Double { val dx = x - o.x; val dy = y - o.y; return sqrt(dx * dx + dy * dy) }
    fun angle(): Double = atan2(y, x)
    /** Rotate counter-clockwise by [a] radians (in a Y-up frame). */
    fun rotate(a: Double): Vec {
        val c = cos(a); val s = sin(a)
        return Vec(x * c - y * s, x * s + y * c)
    }
}

internal object Geo {
    const val TWO_PI = 2 * PI

    fun polar(cx: Double, cy: Double, r: Double, a: Double) = Vec(cx + r * cos(a), cy + r * sin(a))

    fun lerp(a: Vec, b: Vec, t: Double) = Vec(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t)

    fun regularPolygon(cx: Double, cy: Double, r: Double, sides: Int, rotation: Double = -PI / 2): List<Vec> =
        List(sides) { polar(cx, cy, r, rotation + it * TWO_PI / sides) }

    fun edgePoints(a: Vec, b: Vec, count: Int, reverse: Boolean = false): List<Vec> {
        require(count >= 2) { "edgePoints needs at least 2 points" }
        return List(count) { i ->
            val t = i.toDouble() / (count - 1)
            lerp(a, b, if (reverse) 1.0 - t else t)
        }
    }

    /** Distance from [p] to the segment [a]-[b]. */
    fun distToSegment(p: Vec, a: Vec, b: Vec): Double {
        val abx = b.x - a.x; val aby = b.y - a.y
        val l2 = abx * abx + aby * aby
        if (l2 == 0.0) return p.dist(a)
        val t = (((p.x - a.x) * abx + (p.y - a.y) * aby) / l2).coerceIn(0.0, 1.0)
        val dx = a.x + abx * t - p.x; val dy = a.y + aby * t - p.y
        return sqrt(dx * dx + dy * dy)
    }

    /** Normalise an angle into [0, 2π). */
    fun norm(a: Double): Double {
        var r = a % TWO_PI
        if (r < 0) r += TWO_PI
        return r
    }
}
