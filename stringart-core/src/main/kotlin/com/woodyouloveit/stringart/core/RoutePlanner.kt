package com.woodyouloveit.stringart.core

/**
 * @param connectorMaxLength if > 0, odd nails closer than this get an extra visible string between them,
 *        which reduces the number of separate thread runs (fewer tie-offs). 0 = never add strings.
 */
data class RouteOptions(val connectorMaxLength: Double = 0.0)

data class RoutePlan(val runs: List<Run>, val connectorsAdded: List<Chord>)

/**
 * Turns an unordered set of strings into thread runs.
 *  1. Each connected group of strings with k odd-degree nails needs max(1, k/2) runs; the planner reaches
 *     that minimum by pairing odd nails with invisible links, building one Euler circuit (Hierholzer)
 *     and cutting it at the invisible links.
 *  2. Wrap directions per nail are chosen by dynamic programming so the needle and thread paths avoid
 *     other nails, then by shortest total wrap.
 */
object RoutePlanner {
    private const val INFEASIBLE = 1e15
    private const val HIT = 1e6
    /** Costs/distances closer than this count as equal; the first candidate wins (same on every platform). */
    private const val EPS = 1e-7

    fun plan(design: Design, spec: MachineSpec, options: RouteOptions = RouteOptions()): RoutePlan {
        val n = design.nails.size
        val pos = design.nails.map { it.pos }
        val ea = ArrayList<Int>(); val eb = ArrayList<Int>(); val virtual = ArrayList<Boolean>()
        fun addEdge(a: Int, b: Int, v: Boolean) { ea.add(a); eb.add(b); virtual.add(v) }
        design.chords.forEach { addEdge(it.a, it.b, false) }

        val degree = IntArray(n)
        design.chords.forEach { degree[it.a]++; degree[it.b]++ }

        // Optional visible connectors between nearby odd nails.
        val connectors = ArrayList<Chord>()
        if (options.connectorMaxLength > 0) {
            val machine = design.machineNails()
            val probe = CollisionProbe(machine, spec)
            val existing = design.chords.toHashSet()
            val odd = (0 until n).filter { degree[it] % 2 == 1 }
            val candidates = ArrayList<Triple<Double, Int, Int>>()
            for (x in odd.indices) for (y in x + 1 until odd.size) {
                val d = pos[odd[x]].dist(pos[odd[y]])
                if (d <= options.connectorMaxLength) candidates.add(Triple(d, odd[x], odd[y]))
            }
            candidates.sortBy { Math.round(it.first * 1e6) }   // quantised: ties keep their order
            for ((_, a, b) in candidates) {
                if (degree[a] % 2 == 0 || degree[b] % 2 == 0) continue
                val c = Chord.of(a, b)
                if (c in existing) continue
                if (probe.lineHits(machine[a], machine[b], spec.threadClear, a, b) > 0) continue
                addEdge(a, b, false); connectors.add(c); existing.add(c)
                degree[a]++; degree[b]++
            }
        }

        // Connected components (union-find over string endpoints).
        val parent = IntArray(n) { it }
        fun find(x: Int): Int { var r = x; while (parent[r] != r) { parent[r] = parent[parent[r]]; r = parent[r] }; return r }
        for (e in ea.indices) { val ra = find(ea[e]); val rb = find(eb[e]); if (ra != rb) parent[ra] = rb }

        val compNodes = LinkedHashMap<Int, MutableList<Int>>()
        for (v in 0 until n) if (degree[v] > 0) compNodes.getOrPut(find(v)) { ArrayList() }.add(v)

        // Pair odd nails inside each component with invisible links (greedy nearest).
        for (nodes in compNodes.values) {
            val odd = nodes.filter { degree[it] % 2 == 1 }.toMutableList()
            while (odd.isNotEmpty()) {
                val a = odd.removeAt(0)
                var best = 0
                for (k in 1 until odd.size) if (pos[a].dist(pos[odd[k]]) < pos[a].dist(pos[odd[best]]) - EPS) best = k
                addEdge(a, odd.removeAt(best), true)
            }
        }

        val adj = Array(n) { ArrayList<Int>() }
        for (e in ea.indices) { adj[ea[e]].add(e); adj[eb[e]].add(e) }
        val used = BooleanArray(ea.size)
        val ptr = IntArray(n)
        val trails = ArrayList<List<Int>>()

        for (nodes in compNodes.values) {
            // Hierholzer, iterative.
            val stackV = ArrayList<Int>(); val stackE = ArrayList<Int>()
            val popV = ArrayList<Int>(); val popE = ArrayList<Int>()
            stackV.add(nodes.min()); stackE.add(-1)
            while (stackV.isNotEmpty()) {
                val v = stackV.last()
                val list = adj[v]
                while (ptr[v] < list.size && used[list[ptr[v]]]) ptr[v]++
                if (ptr[v] < list.size) {
                    val e = list[ptr[v]]
                    used[e] = true
                    stackV.add(if (ea[e] == v) eb[e] else ea[e]); stackE.add(e)
                } else {
                    popV.add(stackV.removeAt(stackV.size - 1)); popE.add(stackE.removeAt(stackE.size - 1))
                }
            }
            // Built by index on purpose: ArrayList.reversed() binds to a Java 21 method missing on Android < 15.
            val verts = List(popV.size) { popV[popV.size - 1 - it] }
            val edges = List(popE.size - 1) { popE[popE.size - 2 - it] } // edges[k] joins verts[k] and verts[k+1]
            val m = edges.size
            val firstVirtual = edges.indexOfFirst { virtual[it] }
            if (firstVirtual < 0) { trails.add(verts); continue }
            var current = arrayListOf(verts[(firstVirtual + 1) % m])
            for (k in firstVirtual + 1..firstVirtual + m) {
                val idx = k % m
                val next = verts[idx + 1]
                if (virtual[edges[idx]]) {
                    if (current.size > 1) trails.add(current)
                    current = arrayListOf(next)
                } else current.add(next)
            }
        }

        val machine = design.machineNails()
        val probe = CollisionProbe(machine, spec)
        val runs = trails.map { trail ->
            val dirs = chooseDirections(trail, machine, probe, spec)
            Run(design.color, trail.indices.map { Step(trail[it], WrapDir.ofSign(dirs[it])) })
        }
        return RoutePlan(runs, connectors)
    }

    /** Viterbi over states (dir of previous nail, dir of this nail). */
    private fun chooseDirections(trail: List<Int>, m: List<Vec>, probe: CollisionProbe, spec: MachineSpec): IntArray {
        val len = trail.size
        val signs = intArrayOf(-1, 1)
        val segCache = HashMap<Long, Double>()
        fun segCost(i: Int, sa: Int, sb: Int): Double {
            val key = (i.toLong() shl 2) or ((if (sa > 0) 1L else 0L) shl 1) or (if (sb > 0) 1L else 0L)
            return segCache.getOrPut(key) {
                val a = trail[i]; val b = trail[i + 1]
                val needle = Tangents.between(m[a], sa, m[b], sb, spec.wrapRadius)
                val thread = Tangents.between(m[a], sa, m[b], sb, spec.nailR)
                if (needle == null || thread == null) INFEASIBLE
                else HIT * (probe.lineHits(needle.first, needle.second, spec.needleClear, a, b) +
                    probe.lineHits(thread.first, thread.second, spec.threadClear, a, b))
            }
        }
        fun arcCost(i: Int, sp: Int, s: Int, sn: Int): Double {
            val inLine = Tangents.between(m[trail[i - 1]], sp, m[trail[i]], s, spec.wrapRadius) ?: return INFEASIBLE
            val outLine = Tangents.between(m[trail[i]], s, m[trail[i + 1]], sn, spec.wrapRadius) ?: return INFEASIBLE
            val sweep = Tangents.sweep(m[trail[i]], inLine.second, outLine.first, s)
            return HIT * probe.arcHits(trail[i], inLine.second, outLine.first, s) + sweep * spec.wrapRadius
        }

        // dp[a][b]: best cost with dir(i-1)=signs[a], dir(i)=signs[b]
        var dp = Array(2) { a -> DoubleArray(2) { b -> segCost(0, signs[a], signs[b]) } }
        val back = ArrayList<Array<IntArray>>() // back[i][b][c] = best a
        for (i in 1 until len - 1) {
            val next = Array(2) { DoubleArray(2) { Double.MAX_VALUE } }
            val choice = Array(2) { IntArray(2) }
            for (b in 0..1) for (c in 0..1) {
                val seg = segCost(i, signs[b], signs[c])
                for (a in 0..1) {
                    val v = dp[a][b] + arcCost(i, signs[a], signs[b], signs[c]) + seg
                    if (v < next[b][c] - EPS) { next[b][c] = v; choice[b][c] = a }
                }
            }
            back.add(choice); dp = next
        }
        var bestA = 0; var bestB = 0
        for (a in 0..1) for (b in 0..1) if (dp[a][b] < dp[bestA][bestB] - EPS) { bestA = a; bestB = b }
        val out = IntArray(len)
        out[len - 1] = signs[bestB]; out[len - 2] = signs[bestA]
        var b = bestA; var c = bestB
        for (i in len - 3 downTo 0) {
            val a = back[i][b][c]
            out[i] = signs[a]
            c = b; b = a
        }
        return out
    }
}
