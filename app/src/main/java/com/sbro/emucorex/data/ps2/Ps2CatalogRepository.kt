package com.sbro.emucorex.data.ps2

import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.util.Log
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityRepository
import java.io.File
import java.io.FileOutputStream
import java.text.Normalizer
import java.util.Locale
import kotlin.math.max

class Ps2CatalogRepository(private val context: Context) {

    companion object {
        private const val TAG = "Ps2CatalogRepository"
        private const val DB_NAME = "games.db"
        private const val ASSET_PATH = "catalog/games.db"
        private const val MIN_CONFIDENT_TITLE_MATCH_SCORE = 6_500
        private val WEAK_TOKENS = setOf(
            "a", "an", "and", "the", "of", "to", "in", "on", "for", "from", "with",
            "vs", "version", "edition", "special", "limited", "international",
            "at", "part", "pt", "vol", "volume"
        )
    }

    private val dbFile: File by lazy { File(context.noBackupFilesDir, DB_NAME) }
    private var database: SQLiteDatabase? = null
    private val compatibilityRepository = Pcsx2CompatibilityRepository(context)
    private val identityIndexRepository = Ps2IdentityIndexRepository(context)

    private val defaultSortOrder = """
        CASE WHEN rating IS NULL THEN 1 ELSE 0 END,
        rating DESC,
        CASE WHEN year IS NULL THEN 1 ELSE 0 END,
        year DESC,
        name COLLATE NOCASE ASC
    """.trimIndent()

    fun ensureDatabaseReady(): Boolean {
        if (dbFile.exists() && dbFile.length() > 0L) {
            return true
        }

        return runCatching {
            dbFile.parentFile?.mkdirs()
            context.assets.open(ASSET_PATH).use { input ->
                FileOutputStream(dbFile).use { output ->
                    input.copyTo(output)
                }
            }
            true
        }.getOrElse {
            Log.w(TAG, "Catalog DB is not bundled yet: ${it.message}")
            false
        }
    }

    fun hasCatalog(): Boolean = ensureDatabaseReady()

    fun findBestMatch(
        title: String,
        fileName: String,
        serial: String?
    ): Ps2CatalogMatch? {
        if (!ensureDatabaseReady()) return null
        val db = getDatabase() ?: return null

        val candidates = linkedSetOf(
            title,
            fileName.substringBeforeLast('.'),
            cleanupTitle(title),
            cleanupTitle(fileName.substringBeforeLast('.'))
        ).filter { it.isNotBlank() }

        if (candidates.isEmpty()) return null

        identityIndexRepository.findBestMatch(
            serial = serial,
            titleCandidates = candidates.toList()
        )?.let { prebuiltMatch ->
            val summary = getSummaryByIgdbId(db, prebuiltMatch.igdbId) ?: return@let
            return Ps2CatalogMatch(
                details = loadDetails(db, summary),
                score = prebuiltMatch.confidence,
                matchedBySerial = prebuiltMatch.matchedBy.startsWith("serial"),
                matchedBy = prebuiltMatch.matchedBy
            )
        }

        val serialMatch = serial
            ?.let { normalizedSerial(it) }
            ?.takeIf { it.isNotBlank() }
            ?.let { lookupBySerial(db, it) }
        
        if (serialMatch != null) {
            return Ps2CatalogMatch(
                details = loadDetails(db, serialMatch),
                score = 10_000,
                matchedBySerial = true,
                matchedBy = "serial_db"
            )
        }

        val normalizedCandidates = candidates
            .flatMap { buildSearchCandidates(it) }
            .filter { it.isNotBlank() }
            .distinct()

        var best: Ps2CatalogSummary? = null
        var bestCandidate: String? = null
        var bestScore = Int.MIN_VALUE
        for (candidate in normalizedCandidates) {
            val titleMatches = lookupByNormalizedTitle(db, candidate)
            for (summary in titleMatches) {
                val score = calculateTitleScore(candidate, summary)
                if (score > bestScore || (score == bestScore && (bestCandidate == null || candidate.length > bestCandidate.length))) {
                    best = summary
                    bestCandidate = candidate
                    bestScore = score
                }
            }
        }

        val resolvedCandidate = bestCandidate
        return best?.takeIf { resolvedCandidate != null && isConfidentTitleMatch(resolvedCandidate, it, bestScore) }?.let {
            Ps2CatalogMatch(
                details = loadDetails(db, it),
                score = bestScore,
                matchedBySerial = false,
                matchedBy = "title_db"
            )
        }
    }

    fun search(query: String, limit: Int = 60, offset: Int = 0): List<Ps2CatalogSummary> {
        if (!ensureDatabaseReady()) return emptyList()
        return search(
            query = query,
            genre = null,
            year = null,
            minRating = null,
            limit = limit,
            offset = offset
        )
    }

    fun search(
        query: String,
        genre: String?,
        year: Int?,
        minRating: Double?,
        limit: Int = 60,
        offset: Int = 0
    ): List<Ps2CatalogSummary> {
        if (!ensureDatabaseReady()) return emptyList()
        val normalized = normalizeSearchText(query)
        if (normalized.isBlank()) {
            return topRated(
                genre = genre,
                year = year,
                minRating = minRating,
                limit = limit,
                offset = offset
            )
        }

        val db = getDatabase() ?: return emptyList()
        val out = ArrayList<Ps2CatalogSummary>(limit)
        val seen = HashSet<Long>(limit * 2)
        val fetchWindow = limit + offset
        querySearchPage(
            db = db,
            normalizedPattern = "$normalized%",
            genre = genre,
            year = year,
            minRating = minRating,
            limit = fetchWindow
        ).forEach { summary ->
            if (seen.add(summary.igdbId)) {
                out += summary
            }
        }

        if (out.size < fetchWindow) {
            querySearchPage(
                db = db,
                normalizedPattern = "%$normalized%",
                genre = genre,
                year = year,
                minRating = minRating,
                limit = fetchWindow
            ).forEach { summary ->
                if (seen.add(summary.igdbId)) {
                    out += summary
                }
            }
        }

        return out.drop(offset).take(limit)
    }

    fun getDetails(igdbId: Long): Ps2CatalogDetails? {
        if (!ensureDatabaseReady()) return null
        val db = getDatabase() ?: return null
        return db.rawQuery(
            """
            SELECT igdb_id, name, normalized_name, year, rating, summary, storyline, cover_url, hero_url
            FROM games
            WHERE igdb_id = ?
            LIMIT 1
            """.trimIndent(),
            arrayOf(igdbId.toString())
        ).use { cursor ->
            if (!cursor.moveToFirst()) return@use null
            loadDetails(db, cursorToSummary(cursor, db))
        }
    }

    fun topRated(
        genre: String?,
        year: Int?,
        minRating: Double?,
        limit: Int = 60,
        offset: Int = 0
    ): List<Ps2CatalogSummary> {
        if (!ensureDatabaseReady()) return emptyList()
        val db = getDatabase() ?: return emptyList()
        val items = ArrayList<Ps2CatalogSummary>(limit)
        buildCatalogQuery(
            namePattern = null,
            genre = genre,
            year = year,
            minRating = minRating,
            includeOffset = true
        ).let { (sql, args) ->
            db.rawQuery(sql, (args + limit.toString() + offset.toString()).toTypedArray()).use { cursor ->
                while (cursor.moveToNext()) {
                    items += cursorToSummary(cursor, db)
                }
            }
        }
        return items
    }

    fun getAvailableGenres(limit: Int = 18): List<String> {
        if (!ensureDatabaseReady()) return emptyList()
        val db = getDatabase() ?: return emptyList()
        val items = ArrayList<String>(limit)
        db.rawQuery(
            """
            SELECT genre_name, COUNT(*) AS usage_count
            FROM game_genres
            GROUP BY genre_name
            ORDER BY usage_count DESC, genre_name COLLATE NOCASE ASC
            LIMIT ?
            """.trimIndent(),
            arrayOf(limit.toString())
        ).use { cursor ->
            while (cursor.moveToNext()) {
                items += cursor.getString(0)
            }
        }
        return items
    }

    fun getAvailableYears(limit: Int = 30): List<Int> {
        if (!ensureDatabaseReady()) return emptyList()
        val db = getDatabase() ?: return emptyList()
        val items = ArrayList<Int>(limit)
        db.rawQuery(
            """
            SELECT DISTINCT year
            FROM games
            WHERE year IS NOT NULL
            ORDER BY year DESC
            LIMIT ?
            """.trimIndent(),
            arrayOf(limit.toString())
        ).use { cursor ->
            while (cursor.moveToNext()) {
                items += cursor.getInt(0)
            }
        }
        return items
    }

    private fun getDatabase(): SQLiteDatabase? {
        val current = database
        if (current != null && current.isOpen) {
            return current
        }
        
        return try {
            SQLiteDatabase.openDatabase(
                dbFile.absolutePath,
                null,
                SQLiteDatabase.OPEN_READONLY or SQLiteDatabase.NO_LOCALIZED_COLLATORS
            ).also { database = it }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to open catalog database: ${e.message}")
            null
        }
    }

    fun close() {
        database?.close()
        database = null
    }

    private fun lookupBySerial(db: SQLiteDatabase, serial: String): Ps2CatalogSummary? {
        return db.rawQuery(
            """
            SELECT g.igdb_id, g.name, g.normalized_name, g.year, g.rating, g.summary, g.storyline, g.cover_url, g.hero_url
            FROM game_serials s
            JOIN games g ON g.igdb_id = s.igdb_id
            WHERE s.serial = ?
            LIMIT 1
            """.trimIndent(),
            arrayOf(serial)
        ).use { cursor ->
            if (!cursor.moveToFirst()) null else cursorToSummary(cursor, db)
        }
    }

    private fun getSummaryByIgdbId(db: SQLiteDatabase, igdbId: Long): Ps2CatalogSummary? {
        return db.rawQuery(
            """
            SELECT igdb_id, name, normalized_name, year, rating, summary, storyline, cover_url, hero_url
            FROM games
            WHERE igdb_id = ?
            LIMIT 1
            """.trimIndent(),
            arrayOf(igdbId.toString())
        ).use { cursor ->
            if (!cursor.moveToFirst()) null else cursorToSummary(cursor, db)
        }
    }

    private fun lookupByNormalizedTitle(db: SQLiteDatabase, normalizedTitle: String): List<Ps2CatalogSummary> {
        val out = LinkedHashMap<Long, Ps2CatalogSummary>(24)
        val starts = "$normalizedTitle%"
        val contains = "%$normalizedTitle%"
        db.rawQuery(
            """
            SELECT igdb_id, name, normalized_name, year, rating, summary, storyline, cover_url, hero_url
            FROM games
            WHERE normalized_name = ?
               OR normalized_name LIKE ?
               OR normalized_name LIKE ?
            ORDER BY
                CASE
                    WHEN normalized_name = ? THEN 0
                    WHEN normalized_name LIKE ? THEN 1
                    ELSE 2
                END,
                CASE WHEN rating IS NULL THEN 1 ELSE 0 END,
                rating DESC,
                CASE WHEN year IS NULL THEN 1 ELSE 0 END,
                year DESC,
                name COLLATE NOCASE ASC
            LIMIT 16
            """.trimIndent(),
            arrayOf(normalizedTitle, starts, contains, normalizedTitle, starts)
        ).use { cursor ->
            while (cursor.moveToNext()) {
                val summary = cursorToSummary(cursor, db)
                out.putIfAbsent(summary.igdbId, summary)
            }
        }

        val tokenPatterns = buildTokenPatterns(normalizedTitle)
        if (tokenPatterns.isNotEmpty() && out.size < 24) {
            val whereClause = tokenPatterns.joinToString(" AND ") { "normalized_name LIKE ?" }
            db.rawQuery(
                """
                SELECT igdb_id, name, normalized_name, year, rating, summary, storyline, cover_url, hero_url
                FROM games
                WHERE $whereClause
                ORDER BY $defaultSortOrder
                LIMIT 24
                """.trimIndent(),
                tokenPatterns.toTypedArray()
            ).use { cursor ->
                while (cursor.moveToNext()) {
                    val summary = cursorToSummary(cursor, db)
                    out.putIfAbsent(summary.igdbId, summary)
                }
            }
        }
        return out.values.toList()
    }

    private fun querySearchPage(
        db: SQLiteDatabase,
        normalizedPattern: String,
        genre: String?,
        year: Int?,
        minRating: Double?,
        limit: Int
    ): List<Ps2CatalogSummary> {
        val out = ArrayList<Ps2CatalogSummary>(limit)
        buildCatalogQuery(
            namePattern = normalizedPattern,
            genre = genre,
            year = year,
            minRating = minRating,
            includeOffset = false
        ).let { (sql, args) ->
            db.rawQuery(sql, (args + limit.toString()).toTypedArray()).use { cursor ->
                while (cursor.moveToNext()) {
                    out += cursorToSummary(cursor, db)
                }
            }
        }
        return out
    }

    private fun buildCatalogQuery(
        namePattern: String?,
        genre: String?,
        year: Int?,
        minRating: Double?,
        includeOffset: Boolean
    ): Pair<String, List<String>> {
        val where = ArrayList<String>(4)
        val args = ArrayList<String>(4)

        if (!namePattern.isNullOrBlank()) {
            where += "normalized_name LIKE ?"
            args += namePattern
        }
        if (!genre.isNullOrBlank()) {
            where += """
                EXISTS (
                    SELECT 1
                    FROM game_genres gg
                    WHERE gg.igdb_id = games.igdb_id
                      AND gg.genre_name = ?
                )
            """.trimIndent()
            args += genre
        }
        year?.let {
            where += "year = ?"
            args += it.toString()
        }
        minRating?.let {
            where += "rating >= ?"
            args += it.toString()
        }

        val whereClause = if (where.isEmpty()) "" else "WHERE ${where.joinToString(" AND ")}"
        val limitClause = if (includeOffset) "LIMIT ? OFFSET ?" else "LIMIT ?"
        return """
            SELECT igdb_id, name, normalized_name, year, rating, summary, storyline, cover_url, hero_url
            FROM games
            $whereClause
            ORDER BY $defaultSortOrder
            $limitClause
        """.trimIndent() to args
    }

    private fun loadDetails(db: SQLiteDatabase, summary: Ps2CatalogSummary): Ps2CatalogDetails {
        val genres = ArrayList<String>(summary.genres)
        if (genres.isEmpty()) {
            db.rawQuery(
                """
                SELECT genre_name
                FROM game_genres
                WHERE igdb_id = ?
                ORDER BY genre_name COLLATE NOCASE ASC
                """.trimIndent(),
                arrayOf(summary.igdbId.toString())
            ).use { cursor ->
                while (cursor.moveToNext()) {
                    genres += cursor.getString(0)
                }
            }
        }

        val screenshots = ArrayList<String>(10)
        db.rawQuery(
            """
            SELECT image_url
            FROM game_screenshots
            WHERE igdb_id = ?
            ORDER BY position ASC
            LIMIT 10
            """.trimIndent(),
            arrayOf(summary.igdbId.toString())
        ).use { cursor ->
            while (cursor.moveToNext()) {
                toHighResImageUrl(cursor.getString(0))?.let { screenshots += it }
            }
        }

        val videos = ArrayList<String>(10)
        db.rawQuery(
            """
            SELECT youtube_id
            FROM game_videos
            WHERE igdb_id = ?
            ORDER BY position ASC
            LIMIT 10
            """.trimIndent(),
            arrayOf(summary.igdbId.toString())
        ).use { cursor ->
            while (cursor.moveToNext()) {
                videos += cursor.getString(0)
            }
        }

        return Ps2CatalogDetails(
            igdbId = summary.igdbId,
            name = summary.name,
            normalizedName = summary.normalizedName,
            year = summary.year,
            rating = summary.rating,
            storyline = summary.storyline,
            summary = summary.summary,
            genres = genres.distinct(),
            screenshots = screenshots,
            videos = videos,
            coverUrl = toHighResImageUrl(summary.coverUrl),
            heroUrl = toHighResImageUrl(summary.heroUrl),
            primarySerial = summary.primarySerial,
            pcsx2Compatibility = summary.pcsx2Compatibility
        )
    }

    private fun cursorToSummary(cursor: android.database.Cursor, db: SQLiteDatabase): Ps2CatalogSummary {
        val igdbId = cursor.getLong(0)
        val name = cursor.getString(1)
        val compatibility = compatibilityRepository.findByIgdbId(igdbId)
        return Ps2CatalogSummary(
            igdbId = igdbId,
            name = name,
            normalizedName = cursor.getString(2),
            year = cursor.getIntOrNull(3),
            rating = cursor.getDoubleOrNull(4),
            summary = cursor.getStringOrNull(5),
            storyline = cursor.getStringOrNull(6),
            coverUrl = cursor.getStringOrNull(7),
            heroUrl = cursor.getStringOrNull(8),
            genres = loadGenresPreview(db, igdbId),
            primarySerial = compatibility?.serial,
            pcsx2Compatibility = compatibility
        )
    }

    private fun loadGenresPreview(db: SQLiteDatabase, igdbId: Long): List<String> {
        val out = ArrayList<String>(4)
        db.rawQuery(
            """
            SELECT genre_name
            FROM game_genres
            WHERE igdb_id = ?
            ORDER BY genre_name COLLATE NOCASE ASC
            LIMIT 4
            """.trimIndent(),
            arrayOf(igdbId.toString())
        ).use { cursor ->
            while (cursor.moveToNext()) {
                out += cursor.getString(0)
            }
        }
        return out
    }

    private fun calculateTitleScore(candidate: String, summary: Ps2CatalogSummary): Int {
        val target = normalizeSearchText(summary.normalizedName)
        if (candidate == target) return 12_000

        val candidateTokens = tokenize(candidate)
        val targetTokens = tokenize(target)

        var score = 0
        if (target.startsWith(candidate)) {
            score += 6_000 - (target.length - candidate.length) * 12
        } else if (candidate.startsWith(target)) {
            score += 5_200 - (candidate.length - target.length) * 14
        } else if (target.contains(candidate)) {
            score += 4_300 - (target.length - candidate.length) * 10
        }

        score += tokenSetScore(candidateTokens, targetTokens)
        score += orderedTokenScore(candidateTokens, targetTokens)
        score += numericTokenScore(candidateTokens, targetTokens)
        score += significantWordCoverageScore(candidateTokens, targetTokens)
        score += levenshteinScore(candidate, target)

        if (candidateTokens.isNotEmpty() && targetTokens.isNotEmpty()) {
            val candidateJoined = candidateTokens.joinToString("")
            val targetJoined = targetTokens.joinToString("")
            if (candidateJoined == targetJoined) {
                score += 2_000
            } else if (targetJoined.contains(candidateJoined) || candidateJoined.contains(targetJoined)) {
                score += 1_000
            }
        }

        return score
    }

    private fun levenshteinScore(a: String, b: String): Int {
        val distances = IntArray(b.length + 1) { it }
        for (i in 1..a.length) {
            var previous = distances[0]
            distances[0] = i
            for (j in 1..b.length) {
                val temp = distances[j]
                distances[j] = minOf(
                    distances[j] + 1,
                    distances[j - 1] + 1,
                    previous + if (a[i - 1] == b[j - 1]) 0 else 1
                )
                previous = temp
            }
        }
        val distance = distances[b.length]
        val maxLength = max(a.length, b.length).coerceAtLeast(1)
        val similarity = 1f - (distance.toFloat() / maxLength.toFloat())
        return (similarity * 2_400f).toInt() - distance * 20
    }

    private fun cleanupTitle(value: String): String {
        return value
            .substringBeforeLast('.')
            .replace(Regex("""\[[^]]*]|\([^)]*\)"""), " ")
            .replace(Regex("""\b(disc|disk|cd|dvd)\s*[0-9]+\b"""), " ")
            .replace(Regex("""\b(v[0-9]+|ver\s*[0-9]+(\.[0-9]+)?|rev\s*[a-z0-9]+|patch\s*[0-9]+)\b"""), " ")
            .replace(Regex("""\b(usa|us|ntsc|pal|eur|europe|japan|jpn|korea|kor|france|germany|spain|esp|italy|russia|russian|beta|demo|proto|prototype|undub|unl|translated|en|fr|de|es|it|ru|pt|pl|ko|ja|jp)\b"""), " ")
            .replace(Regex("""\s+"""), " ")
            .trim()
    }

    private fun normalizeSearchText(value: String): String {
        val cleaned = cleanupTitle(value).lowercase(Locale.ROOT)
        val normalized = Normalizer.normalize(cleaned, Normalizer.Form.NFD)
            .replace(Regex("\\p{Mn}+"), "")
        return normalized
            .replace("&", " and ")
            .replace("@", " at ")
            .replace(Regex("""\bviii\b"""), " 8 ")
            .replace(Regex("""\bvii\b"""), " 7 ")
            .replace(Regex("""\bvi\b"""), " 6 ")
            .replace(Regex("""\biv\b"""), " 4 ")
            .replace(Regex("""\biii\b"""), " 3 ")
            .replace(Regex("""\bii\b"""), " 2 ")
            .replace(Regex("""\bix\b"""), " 9 ")
            .replace(Regex("""\bxii\b"""), " 12 ")
            .replace(Regex("""\bxi\b"""), " 11 ")
            .replace(Regex("""\bv\b"""), " 5 ")
            .replace(Regex("""\bx\b"""), " 10 ")
            .replace(Regex("""[^a-z0-9]+"""), " ")
            .replace(Regex("""\s+"""), " ")
            .trim()
    }

    private fun buildSearchCandidates(value: String): List<String> {
        val normalized = normalizeSearchText(value)
        if (normalized.isBlank()) return emptyList()

        val tokens = tokenize(normalized)
        if (tokens.isEmpty()) return listOf(normalized)

        val variants = linkedSetOf(normalized)
        val significant = tokens.filterNot(::isWeakToken)
        if (significant.isNotEmpty()) {
            variants += significant.joinToString(" ")
        }

        val cleaned = cleanupTitle(value)
        listOf(':', '|').forEach { separator ->
            val prefix = cleaned.substringBefore(separator).trim()
            val normalizedPrefix = normalizeSearchText(prefix)
            if (normalizedPrefix.isNotBlank()) {
                variants += normalizedPrefix
            }
        }

        return variants.toList()
    }

    private fun buildTokenPatterns(normalizedTitle: String): List<String> {
        val tokens = tokenize(normalizedTitle)
        val patterns = tokens
            .asSequence()
            .filterNot(::isWeakToken)
            .filterNot { it.all(Char::isDigit) }
            .distinct()
            .sortedByDescending { it.length }
            .take(4)
            .map { "%$it%" }
            .toMutableList()

        if (patterns.isEmpty() && tokens.isNotEmpty()) {
            patterns += tokens.take(2).map { "%$it%" }
        }

        return patterns
    }

    private fun tokenize(value: String): List<String> {
        return value.split(' ').map { it.trim() }.filter { it.isNotBlank() }
    }

    private fun tokenSetScore(candidateTokens: List<String>, targetTokens: List<String>): Int {
        if (candidateTokens.isEmpty() || targetTokens.isEmpty()) return 0
        val candidateSet = candidateTokens.toSet()
        val targetSet = targetTokens.toSet()
        val overlap = candidateSet.intersect(targetSet).size
        if (overlap == 0) return -1_200
        val union = candidateSet.union(targetSet).size.coerceAtLeast(1)
        return (overlap * 3_600) / union
    }

    private fun orderedTokenScore(candidateTokens: List<String>, targetTokens: List<String>): Int {
        if (candidateTokens.isEmpty() || targetTokens.isEmpty()) return 0
        var matched = 0
        var targetIndex = 0
        for (token in candidateTokens) {
            while (targetIndex < targetTokens.size && targetTokens[targetIndex] != token) {
                targetIndex++
            }
            if (targetIndex >= targetTokens.size) break
            matched++
            targetIndex++
        }
        return matched * 650
    }

    private fun numericTokenScore(candidateTokens: List<String>, targetTokens: List<String>): Int {
        val candidateNumbers = candidateTokens.filter { it.all(Char::isDigit) }
        val targetNumbers = targetTokens.filter { it.all(Char::isDigit) }
        if (candidateNumbers.isEmpty() && targetNumbers.isEmpty()) return 0
        
        val candSet = candidateNumbers.toSet()
        val targetSet = targetNumbers.toSet()
        
        if (candSet == targetSet) return 1_400
        val diff = if (candSet.size > targetSet.size) candSet - targetSet else targetSet - candSet
        if (diff == setOf("1")) return 700 
        
        return -3_000
    }

    private fun significantWordCoverageScore(candidateTokens: List<String>, targetTokens: List<String>): Int {
        val significant = candidateTokens.filterNot(::isWeakToken)
        if (significant.isEmpty()) return 0
        val targetSet = targetTokens.toSet()
        val matched = significant.count { it in targetSet }
        val missing = significant.size - matched
        return matched * 900 - missing * 500
    }

    private fun isConfidentTitleMatch(candidate: String, summary: Ps2CatalogSummary, score: Int): Boolean {
        if (score < MIN_CONFIDENT_TITLE_MATCH_SCORE) return false

        val candidateTokens = tokenize(candidate).filterNot(::isWeakToken)
        val targetTokens = tokenize(normalizeSearchText(summary.normalizedName)).filterNot(::isWeakToken)

        if (candidateTokens.isEmpty() || targetTokens.isEmpty()) {
            return true
        }

        val candidateNumbers = candidateTokens.filter { it.all(Char::isDigit) }.toSet()
        val targetNumbers = targetTokens.filter { it.all(Char::isDigit) }.toSet()

        val candNumsFixed = candidateNumbers.filter { it != "1" }.toSet()
        val targetNumsFixed = targetNumbers.filter { it != "1" }.toSet()

        if (candNumsFixed != targetNumsFixed) return false


        val candidateSet = candidateTokens.toSet()
        val targetSet = targetTokens.toSet()
        

        val missingFromTarget = candidateSet.filter { it !in targetSet }
        if (missingFromTarget.isNotEmpty()) return false
        

        val extraInTarget = targetSet.filter { it !in candidateSet && it != "1" }
        if (extraInTarget.size > 1) return false

        val matchedLongTokens = candidateTokens
            .filter { it.length >= 4 }
            .count { it in targetSet }
        val totalLongTokens = candidateTokens.count { it.length >= 4 }
        return !(totalLongTokens >= 2 && matchedLongTokens * 2 < totalLongTokens)
    }

    private fun isWeakToken(token: String): Boolean {
        return (token.length <= 1 && !token.any { it.isDigit() }) || token in WEAK_TOKENS
    }

    private fun normalizedSerial(value: String): String {
        return value.uppercase(Locale.ROOT)
            .replace(Regex("""[^A-Z0-9]"""), "")
    }

    private fun toHighResImageUrl(url: String?): String? {
        if (url.isNullOrBlank()) return url
        return url.replace(Regex("""/t_[^/]+/"""), "/t_1080p/")
    }
}

private fun android.database.Cursor.getStringOrNull(index: Int): String? {
    return if (isNull(index)) null else getString(index)
}

private fun android.database.Cursor.getIntOrNull(index: Int): Int? {
    return if (isNull(index)) null else getInt(index)
}

private fun android.database.Cursor.getDoubleOrNull(index: Int): Double? {
    return if (isNull(index)) null else getDouble(index)
}
