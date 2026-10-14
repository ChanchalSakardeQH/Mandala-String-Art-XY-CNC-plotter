package com.woodyouloveit.stringart.core

import kotlin.math.floor

/** A nail on the board. Coordinates are in mm, origin top-left, Y pointing down (same as SVG / screen). */
data class Nail(val x: Double, val y: Double, val group: Int = 0) {
    val pos: Vec get() = Vec(x, y)
}

/** An undirected string between two nails, stored with a < b. Create with [Chord.of]. */
data class Chord(val a: Int, val b: Int) {
    init { require(a < b) { "Chord must be stored with a < b; use Chord.of()" } }

    companion object {
        fun of(i: Int, j: Int): Chord {
            require(i != j) { "A chord needs two different nails" }
            return if (i < j) Chord(i, j) else Chord(j, i)
        }
    }
}

/**
 * A finished design: every chord endpoint is a nail index, so the design is physically defined.
 * Coordinates use the preview frame (Y down). Conversion to machine frame happens in one place: [toMachine].
 */
class Design(
    val name: String,
    val width: Double,
    val height: Double,
    val nails: List<Nail>,
    val chords: List<Chord>,
    val color: String,
    /** Strings the generator produced twice and the builder merged. */
    val duplicateChordsMerged: Int = 0,
) {
    fun withoutChords(remove: Set<Chord>): Design =
        Design(name, width, height, nails, chords.filterNot { it in remove }, color, duplicateChordsMerged)

    /** Nail centres in the machine frame (Y up, origin bottom-left of the board). */
    fun machineNails(): List<Vec> = nails.map { toMachine(it.pos, height) }

    companion object {
        fun toMachine(p: Vec, boardHeight: Double) = Vec(p.x, boardHeight - p.y)
    }
}

/**
 * Builds a [Design] from raw points. Points closer than [snapTolerance] become the same nail,
 * so shared corners and repeated anchors never create duplicate nails.
 */
class DesignBuilder(
    private val name: String,
    private val width: Double,
    private val height: Double,
    private val color: String,
    private val snapTolerance: Double = 0.01,
) {
    private val nails = ArrayList<Nail>()
    private val grid = HashMap<Long, MutableList<Int>>()
    private val chords = LinkedHashSet<Chord>()
    private var duplicates = 0

    private fun cell(v: Double) = floor(v / snapTolerance).toInt()
    private fun key(ix: Int, iy: Int) = (ix.toLong() shl 32) or (iy.toLong() and 0xffffffffL)

    /** Returns the index of the nail at [p], creating it if needed. */
    fun nail(p: Vec, group: Int = 0): Int {
        val ix = cell(p.x); val iy = cell(p.y)
        for (dx in -1..1) for (dy in -1..1) {
            grid[key(ix + dx, iy + dy)]?.forEach { idx ->
                if (nails[idx].pos.dist(p) <= snapTolerance) return idx
            }
        }
        nails.add(Nail(p.x, p.y, group))
        val idx = nails.size - 1
        grid.getOrPut(key(ix, iy)) { ArrayList(2) }.add(idx)
        return idx
    }

    /** Adds a string between two points (they become nails). Strings shorter than 0.1 mm are ignored. */
    fun chord(pa: Vec, pb: Vec, groupA: Int = 0, groupB: Int = groupA) {
        if (pa.dist(pb) <= 0.1) return
        val ia = nail(pa, groupA)
        val ib = nail(pb, groupB)
        if (ia == ib) return
        if (!chords.add(Chord.of(ia, ib))) duplicates++
    }

    fun build() = Design(name, width, height, nails.toList(), chords.toList(), color, duplicates)
}
