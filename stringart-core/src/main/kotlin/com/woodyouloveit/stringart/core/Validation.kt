package com.woodyouloveit.stringart.core

enum class Severity { ERROR, WARNING, INFO }

/**
 * @param code stable identifier for the app to translate / react to.
 * @param count how many places are affected; [examples] lists the first few (nail indices or "run:step").
 */
data class Issue(val severity: Severity, val code: String, val message: String, val count: Int, val examples: List<String>)

data class Report(val issues: List<Issue>) {
    val errors get() = issues.filter { it.severity == Severity.ERROR }
    val ok get() = errors.isEmpty()
    fun count(code: String) = issues.firstOrNull { it.code == code }?.count ?: 0
}

private class IssueCollector {
    private val map = LinkedHashMap<String, Triple<Severity, String, MutableList<String>>>()
    private val counts = HashMap<String, Int>()
    fun add(sev: Severity, code: String, message: String, example: String) {
        val e = map.getOrPut(code) { Triple(sev, message, ArrayList()) }
        counts[code] = (counts[code] ?: 0) + 1
        if (e.third.size < 5) e.third.add(example)
    }
    fun report() = Report(map.map { (code, v) -> Issue(v.first, code, v.second, counts[code] ?: 0, v.third) })
}

object Validator {

    /** Checks that do not depend on routing: spec sanity, board fit, nail spacing, unused nails. */
    fun checkDesign(design: Design, spec: MachineSpec): Report {
        val c = IssueCollector()
        if (spec.wrapRadius < spec.needleClear)
            c.add(Severity.ERROR, "SPEC_WRAP_RADIUS", "Wrap radius is smaller than nail radius + needle radius + clearance (${"%.2f".format(spec.needleClear)} mm)", "wrapRadius=${spec.wrapRadius}")
        if (spec.workWidth != null && design.width > spec.workWidth)
            c.add(Severity.ERROR, "BOARD_TOO_WIDE", "Board is wider than the machine work area", "${design.width} > ${spec.workWidth}")
        if (spec.workHeight != null && design.height > spec.workHeight)
            c.add(Severity.ERROR, "BOARD_TOO_TALL", "Board is taller than the machine work area", "${design.height} > ${spec.workHeight}")

        val nails = design.nails
        val edge = spec.wrapRadius + spec.needleR
        nails.forEachIndexed { i, n ->
            if (n.x < edge || n.y < edge || n.x > design.width - edge || n.y > design.height - edge)
                c.add(Severity.WARNING, "NAIL_NEAR_EDGE", "Needle would leave the board while wrapping these nails", "$i")
        }
        val needlePass = spec.nailDiameter + spec.needleDiameter
        val fullWrap = spec.wrapRadius + spec.needleClear
        for (i in nails.indices) for (j in i + 1 until nails.size) {
            val d = nails[i].pos.dist(nails[j].pos)
            if (d < needlePass)
                c.add(Severity.ERROR, "NAILS_TOO_CLOSE", "Needle cannot pass between nails closer than ${"%.2f".format(needlePass)} mm", "$i-$j")
            else if (d < fullWrap)
                c.add(Severity.WARNING, "NAILS_TIGHT", "Nails closer than ${"%.2f".format(fullWrap)} mm: some wrap directions will hit the neighbour", "$i-$j")
        }
        val used = BooleanArray(nails.size)
        design.chords.forEach { used[it.a] = true; used[it.b] = true }
        used.forEachIndexed { i, u -> if (!u) c.add(Severity.INFO, "UNUSED_NAIL", "Nails with no string (can be left out of the board)", "$i") }
        if (design.duplicateChordsMerged > 0)
            c.add(Severity.INFO, "DUPLICATE_STRINGS_MERGED", "Generator produced ${design.duplicateChordsMerged} duplicate strings; merged", "${design.duplicateChordsMerged}")
        return c.report()
    }

    /** Checks the exact needle and thread paths of a routed job. */
    fun checkJob(job: Job, spec: MachineSpec): Report {
        val c = IssueCollector()
        val m = job.machineNails()
        val probe = CollisionProbe(m, spec)
        job.runs.forEachIndexed { r, run ->
            val st = run.steps
            if (st.size < 2) { c.add(Severity.WARNING, "EMPTY_RUN", "Run with fewer than two nails", "$r"); return@forEachIndexed }
            var arrival: Vec? = null
            for (i in 0 until st.size - 1) {
                val a = st[i]; val b = st[i + 1]
                val tag = "$r:${i + 1}"
                val needle = Tangents.between(m[a.nail], a.dir.sign, m[b.nail], b.dir.sign, spec.wrapRadius)
                val thread = Tangents.between(m[a.nail], a.dir.sign, m[b.nail], b.dir.sign, spec.nailR)
                if (needle == null || thread == null) {
                    c.add(Severity.ERROR, "IMPOSSIBLE_CROSSING", "Opposite wraps on nails too close together", tag); arrival = null; continue
                }
                if (probe.lineHits(needle.first, needle.second, spec.needleClear, a.nail, b.nail) > 0)
                    c.add(Severity.ERROR, "NEEDLE_HITS_NAIL", "Needle path between two nails hits another nail", tag)
                if (probe.lineHits(thread.first, thread.second, spec.threadClear, a.nail, b.nail) > 0)
                    c.add(Severity.ERROR, "THREAD_HITS_NAIL", "String passes over another nail", tag)
                val from = arrival ?: needle.first // first nail: full anchor turn
                if (probe.arcHits(a.nail, from, needle.first, a.dir.sign) > 0)
                    c.add(Severity.ERROR, "WRAP_HITS_NAIL", "Needle hits a neighbouring nail while wrapping", "$r:$i")
                arrival = needle.second
            }
            val last = st.last()
            if (arrival != null && probe.arcHits(last.nail, arrival, arrival, last.dir.sign) > 0)
                c.add(Severity.ERROR, "WRAP_HITS_NAIL", "Needle hits a neighbouring nail while wrapping", "$r:${st.size - 1}")
        }
        return c.report()
    }

    /** Strings whose straight path is blocked; removing them is the auto-fix. */
    internal fun blockedChords(job: Job, spec: MachineSpec): Set<Chord> {
        val m = job.machineNails()
        val probe = CollisionProbe(m, spec)
        val out = HashSet<Chord>()
        for (run in job.runs) for (i in 0 until run.steps.size - 1) {
            val a = run.steps[i]; val b = run.steps[i + 1]
            val needle = Tangents.between(m[a.nail], a.dir.sign, m[b.nail], b.dir.sign, spec.wrapRadius)
            val thread = Tangents.between(m[a.nail], a.dir.sign, m[b.nail], b.dir.sign, spec.nailR)
            val blocked = needle == null || thread == null ||
                probe.lineHits(needle.first, needle.second, spec.needleClear, a.nail, b.nail) > 0 ||
                probe.lineHits(thread.first, thread.second, spec.threadClear, a.nail, b.nail) > 0
            if (blocked) out.add(Chord.of(a.nail, b.nail))
        }
        return out
    }
}

data class BuildOptions(
    val route: RouteOptions = RouteOptions(),
    /** Remove strings that cannot be wound and re-plan. The removed strings are reported. */
    val autoFix: Boolean = false,
    val maxFixRounds: Int = 4,
)

data class BuildResult(
    val design: Design,
    val job: Job,
    val designReport: Report,
    val jobReport: Report,
    val removedChords: List<Chord>,
    val connectorsAdded: List<Chord>,
)

/** The one call the app needs: design -> routed, checked job. */
object StringArtPipeline {
    fun build(design: Design, spec: MachineSpec, options: BuildOptions = BuildOptions()): BuildResult {
        var current = design
        val removed = ArrayList<Chord>()
        var round = 0
        while (true) {
            val plan = RoutePlanner.plan(current, spec, options.route)
            val nails = current.nails.map { it.pos }
            val job = Job(
                name = current.name,
                width = current.width,
                height = current.height,
                nailDiameter = spec.nailDiameter,
                nails = nails,
                runs = plan.runs,
                stats = Job.computeStats(nails, current.height, plan.runs, spec.nailDiameter),
            )
            val blocked = if (options.autoFix && round < options.maxFixRounds) Validator.blockedChords(job, spec) else emptySet()
            if (blocked.isEmpty()) {
                return BuildResult(current, job, Validator.checkDesign(current, spec), Validator.checkJob(job, spec), removed, plan.connectorsAdded)
            }
            removed.addAll(blocked)
            current = current.withoutChords(blocked)
            round++
        }
    }
}
