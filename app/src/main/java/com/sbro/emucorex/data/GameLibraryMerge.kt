package com.sbro.emucorex.data

/**
 * Result of scanning one library root. [complete] is true only when the whole tree was enumerated
 * without any failure (unreadable root, failed sub-directory listing, truncated by limits).
 * An incomplete outcome is additive only: games under it are never dropped.
 */
data class LibraryRootScan(
    val root: String,
    val games: List<GameItem>,
    val complete: Boolean
)

data class LibraryMergeResult(
    val games: List<GameItem>,
    val changed: Boolean
)

object GameLibraryMerge {

    /** True when [gamePath] lives under library [root] (plain folder or SAF tree). */
    fun isUnderRoot(root: String, gamePath: String): Boolean {
        if (root.startsWith("content://")) {
            val base = root.substringBefore("/document/").trimEnd('/')
            return gamePath.startsWith("$base/document/")
        }
        val base = root.trimEnd('/', '\\')
        return gamePath.startsWith("$base/") || gamePath.startsWith("$base\\")
    }

    /**
     * Applies [scans] to [current] as a diff: adds new, updates changed, drops games that vanished
     * from roots that were fully enumerated. Games under incomplete roots, or not attributable to any
     * scanned root, are kept. Ordering matches a full scan (title, case-insensitive, stable).
     */
    fun merge(current: List<GameItem>, scans: List<LibraryRootScan>): LibraryMergeResult {
        val scanned = scans.flatMap { it.games }.distinctBy { it.path }
        val scannedPaths = scanned.mapTo(HashSet()) { it.path }
        val completeRoots = scans.filter { it.complete }.map { it.root }
        // Unscanned games survive unless they sit under a fully enumerated root.
        val kept = current.filter { game ->
            game.path !in scannedPaths && completeRoots.none { isUnderRoot(it, game.path) }
        }
        val result = (scanned + kept).sortedBy { it.title.lowercase() }
        return LibraryMergeResult(result, result != current)
    }
}
