package com.sbro.emucorex.core

/** One TRACK entry of a CUE sheet with its resolved INDEX 01 position. */
internal data class CueTrackInfo(
    val number: Int,
    val mode: String,
    val startFrame: Long
)

/** The tracks assigned to a single FILE entry of a CUE sheet. */
internal data class CueFileBlock(
    val fileName: String?,
    val tracks: List<CueTrackInfo>
)

internal data class CueSheet(
    val blocks: List<CueFileBlock>
) {
    val tracks: List<CueTrackInfo> get() = blocks.flatMap(CueFileBlock::tracks)

    val hasAudioTracks: Boolean
        get() = tracks.any { it.mode.equals("AUDIO", ignoreCase = true) }

    /**
     * Block that holds the data track for [fileName], falling back to the first block with
     * any data track when the CUE references a different file name spelling.
     */
    fun dataBlockFor(fileName: String?): CueFileBlock? {
        val normalized = normalizedName(fileName)
        val matching = normalized?.let { target ->
            blocks.firstOrNull { normalizedName(it.fileName)?.equals(target, ignoreCase = true) == true }
        }
        return matching ?: blocks.firstOrNull { block -> block.tracks.any(CueTrackInfo::isDataTrack) }
    }
}

internal fun CueTrackInfo.isDataTrack(): Boolean =
    mode.startsWith("MODE", ignoreCase = true)

internal fun CueTrackInfo.sectorBytes(): Int =
    if (mode.endsWith("/2048", ignoreCase = true)) 2048 else 2352

/** Frame of the next track in the same block, used as the exclusive end of the data track. */
internal fun CueFileBlock.trackEndFrame(track: CueTrackInfo): Long? =
    tracks.filter { it.startFrame > track.startFrame }
        .minByOrNull(CueTrackInfo::startFrame)
        ?.startFrame

internal fun parseCueSheet(content: String): CueSheet? {
    val blocks = mutableListOf<CueFileBlock>()
    var currentFileName: String? = null
    var currentTracks = mutableListOf<CueTrackInfo>()

    fun flushBlock() {
        if (currentTracks.isNotEmpty()) {
            blocks += CueFileBlock(fileName = currentFileName, tracks = currentTracks.toList())
        }
        currentTracks = mutableListOf()
    }

    content.lineSequence().forEach { rawLine ->
        val line = rawLine.trim()
        if (line.isEmpty() || line.startsWith("REM", ignoreCase = true)) return@forEach
        when (line.substringBefore(' ').uppercase()) {
            "FILE" -> {
                flushBlock()
                currentFileName = extractQuotedValue(line.substringAfter(' '))
            }
            "TRACK" -> {
                val parts = line.split(Regex("\\s+"), limit = 3)
                val number = parts.getOrNull(1)?.toIntOrNull() ?: return@forEach
                val mode = parts.getOrNull(2)?.trim().orEmpty()
                currentTracks += CueTrackInfo(number = number, mode = mode, startFrame = 0L)
            }
            "INDEX" -> {
                val parts = line.split(Regex("\\s+"), limit = 3)
                if (parts.getOrNull(1)?.toIntOrNull() != 1) return@forEach
                val frame = parts.getOrNull(2)?.let(::parseCueFrame) ?: return@forEach
                val index = currentTracks.indexOfLast { it.startFrame == 0L }
                if (index >= 0) {
                    currentTracks[index] = currentTracks[index].copy(startFrame = frame)
                }
            }
        }
    }
    flushBlock()

    return CueSheet(blocks).takeIf { it.blocks.isNotEmpty() }
}

internal fun parseCueFrame(value: String): Long? {
    val parts = value.trim().split(':')
    if (parts.size != 3) return null
    val minutes = parts[0].toLongOrNull() ?: return null
    val seconds = parts[1].toLongOrNull() ?: return null
    val frames = parts[2].toLongOrNull() ?: return null
    if (seconds !in 0..59 || frames !in 0..74) return null
    return (minutes * 60L + seconds) * 75L + frames
}

private fun normalizedName(fileName: String?): String? =
    fileName?.substringAfterLast('/')?.substringAfterLast('\\')?.trim()?.takeIf { it.isNotEmpty() }

private fun extractQuotedValue(value: String): String? {
    val quoted = Regex("\"([^\"]+)\"").find(value)?.groupValues?.getOrNull(1)
    return quoted ?: value.substringBefore(' ').trim().takeIf { it.isNotEmpty() }
}
