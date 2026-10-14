package com.woodyouloveit.stringart.core

import java.util.Locale

/** Job format v1 <-> JSON. No dependencies so the module stays plain Kotlin; see docs/JOB_FORMAT.md. */
object JobJson {

    fun write(job: Job): String {
        val sb = StringBuilder()
        fun num(v: Double) = String.format(Locale.ROOT, "%.3f", v).trimEnd('0').trimEnd('.').let { if (it == "-0") "0" else it }
        sb.append("{\n")
        sb.append("  \"format\": \"").append(Job.FORMAT).append("\",\n")
        sb.append("  \"version\": ").append(job.version).append(",\n")
        sb.append("  \"name\": ").append(quote(job.name)).append(",\n")
        sb.append("  \"units\": \"mm\",\n")
        sb.append("  \"board\": { \"width\": ").append(num(job.width)).append(", \"height\": ").append(num(job.height)).append(", \"yAxis\": \"down\" },\n")
        sb.append("  \"nail\": { \"diameter\": ").append(num(job.nailDiameter)).append(" },\n")
        sb.append("  \"nails\": [")
        job.nails.forEachIndexed { i, p ->
            if (i > 0) sb.append(",")
            sb.append(if (i % 8 == 0) "\n    " else " ").append("[").append(num(p.x)).append(", ").append(num(p.y)).append("]")
        }
        sb.append("\n  ],\n")
        sb.append("  \"runs\": [")
        job.runs.forEachIndexed { r, run ->
            if (r > 0) sb.append(",")
            sb.append("\n    { \"color\": ").append(quote(run.color)).append(", \"steps\": [")
            run.steps.forEachIndexed { i, s ->
                if (i > 0) sb.append(",")
                sb.append(if (i % 10 == 0) "\n      " else " ").append("[").append(s.nail).append(", \"").append(s.dir.json).append("\"]")
            }
            sb.append("\n    ] }")
        }
        sb.append("\n  ],\n")
        val st = job.stats
        sb.append("  \"stats\": { \"runs\": ").append(st.runs).append(", \"wraps\": ").append(st.wraps)
            .append(", \"chords\": ").append(st.chords).append(", \"threadLengthM\": ").append(num(st.threadLengthM)).append(" }\n")
        sb.append("}\n")
        return sb.toString()
    }

    fun read(text: String): Job {
        val root = Parser(text).parseValue() as? Map<*, *> ?: throw IllegalArgumentException("Job JSON must be an object")
        require(root["format"] == Job.FORMAT) { "Not a string art job (format=${root["format"]})" }
        val version = (root["version"] as Double).toInt()
        require(version == Job.FORMAT_VERSION) { "Unsupported job version $version" }
        val board = root["board"] as Map<*, *>
        require((board["yAxis"] ?: "down") == "down") { "Only yAxis=down is supported" }
        val nails = (root["nails"] as List<*>).map { val p = it as List<*>; Vec(p[0] as Double, p[1] as Double) }
        val runs = (root["runs"] as List<*>).map { r ->
            val run = r as Map<*, *>
            Run(run["color"] as String, (run["steps"] as List<*>).map { s ->
                val step = s as List<*>
                val idx = (step[0] as Double).toInt()
                require(idx in nails.indices) { "Step refers to missing nail $idx" }
                Step(idx, WrapDir.ofJson(step[1] as String))
            })
        }
        val nailDiameter = (root["nail"] as Map<*, *>)["diameter"] as Double
        val height = board["height"] as Double
        return Job(
            name = root["name"] as? String ?: "",
            width = board["width"] as Double,
            height = height,
            nailDiameter = nailDiameter,
            nails = nails,
            runs = runs,
            // Recomputed rather than trusted, so stats always match the steps.
            stats = Job.computeStats(nails, height, runs, nailDiameter),
            version = version,
        )
    }

    private fun quote(s: String): String {
        val sb = StringBuilder("\"")
        for (ch in s) when {
            ch == '"' -> sb.append("\\\"")
            ch == '\\' -> sb.append("\\\\")
            ch == '\n' -> sb.append("\\n")
            ch < ' ' -> sb.append(String.format(Locale.ROOT, "\\u%04x", ch.code))
            else -> sb.append(ch)
        }
        return sb.append('"').toString()
    }

    private class Parser(private val s: String) {
        private var i = 0
        private fun ws() { while (i < s.length && s[i].isWhitespace()) i++ }
        private fun fail(msg: String): Nothing = throw IllegalArgumentException("JSON error at $i: $msg")

        fun parseValue(): Any? {
            ws()
            if (i >= s.length) fail("unexpected end")
            return when (val ch = s[i]) {
                '{' -> obj()
                '[' -> arr()
                '"' -> str()
                't' -> lit("true", true)
                'f' -> lit("false", false)
                'n' -> lit("null", null)
                else -> if (ch == '-' || ch.isDigit()) num() else fail("unexpected '$ch'")
            }
        }
        private fun lit(word: String, v: Any?): Any? {
            if (!s.startsWith(word, i)) fail("expected $word")
            i += word.length; return v
        }
        private fun num(): Double {
            val start = i
            while (i < s.length && (s[i].isDigit() || s[i] in "+-.eE")) i++
            return s.substring(start, i).toDoubleOrNull() ?: fail("bad number")
        }
        private fun str(): String {
            i++
            val sb = StringBuilder()
            while (true) {
                if (i >= s.length) fail("unterminated string")
                val ch = s[i++]
                when (ch) {
                    '"' -> return sb.toString()
                    '\\' -> {
                        when (val e = s[i++]) {
                            'n' -> sb.append('\n'); 't' -> sb.append('\t'); 'r' -> sb.append('\r')
                            'b' -> sb.append('\b'); 'f' -> sb.append('\u000c')
                            'u' -> { sb.append(s.substring(i, i + 4).toInt(16).toChar()); i += 4 }
                            else -> sb.append(e)
                        }
                    }
                    else -> sb.append(ch)
                }
            }
        }
        private fun arr(): List<Any?> {
            i++
            val out = ArrayList<Any?>()
            ws()
            if (s[i] == ']') { i++; return out }
            while (true) {
                out.add(parseValue()); ws()
                when (s[i++]) { ',' -> continue; ']' -> return out; else -> fail("expected , or ]") }
            }
        }
        private fun obj(): Map<String, Any?> {
            i++
            val out = LinkedHashMap<String, Any?>()
            ws()
            if (s[i] == '}') { i++; return out }
            while (true) {
                ws(); if (s[i] != '"') fail("expected key")
                val k = str(); ws()
                if (s[i++] != ':') fail("expected :")
                out[k] = parseValue(); ws()
                when (s[i++]) { ',' -> continue; '}' -> return out; else -> fail("expected , or }") }
            }
        }
    }
}
