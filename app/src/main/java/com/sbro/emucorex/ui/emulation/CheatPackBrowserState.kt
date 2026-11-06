package com.sbro.emucorex.ui.emulation

import com.sbro.emucorex.data.RemoteCheatPack
import com.sbro.emucorex.ui.cheats.RemoteCheatSelection
import com.sbro.emucorex.ui.cheats.indexRemoteCheatCatalog
import com.sbro.emucorex.ui.cheats.selectRemoteCheatPacks
import java.util.Locale

/** Loading phases of the in-game cheat pack catalog. */
internal enum class CheatPackBrowserPhase {
    Idle,
    Loading,
    Content,
    Empty,
    Error
}

/**
 * State of the cheat pack browser shown in the in-game Cheats tab. It lives in
 * the view model rather than in the menu composable so downloads survive the
 * menu being closed and the catalog is not refetched on every reopen.
 */
internal data class CheatPackBrowserState(
    val phase: CheatPackBrowserPhase = CheatPackBrowserPhase.Idle,
    val identityKey: String = "",
    val serial: String = "",
    val crc: String = "",
    val compatiblePacks: List<RemoteCheatPack> = emptyList(),
    val otherVersionPacks: List<RemoteCheatPack> = emptyList(),
    val workingPackId: String? = null,
    val importing: Boolean = false,
    val installedPackIds: Set<String> = emptySet(),
    val failedPackId: String? = null,
    val importSucceeded: Boolean? = null
) {
    val isBusy: Boolean
        get() = workingPackId != null || importing
}

/**
 * Resolves the PNACH file name/key for the running game. The CRC-qualified key
 * is preferred because it is what [com.sbro.emucorex.data.CheatRepository]
 * looks up first and what the emulator expects for an active cheat file. When
 * the CRC is unknown the serial is used so the installed pack can still be
 * found by the in-game cheat refresh; a user-supplied file name is the last
 * resort for imports without any detectable game identity.
 */
internal fun resolveCheatPackGameKey(
    serial: String,
    crc: String,
    fallbackName: String? = null
): String? {
    val normalizedSerial = serial.trim()
    val normalizedCrc = crc.trim().uppercase(Locale.US)
    return when {
        normalizedSerial.isNotEmpty() && normalizedCrc.isNotEmpty() -> "${normalizedSerial}_$normalizedCrc"
        normalizedCrc.isNotEmpty() -> normalizedCrc
        normalizedSerial.isNotEmpty() -> normalizedSerial
        else -> fallbackName?.trim()?.takeIf(String::isNotEmpty)
    }
}

/**
 * The online catalog intentionally lists every upstream collection, so one game
 * can be offered by a dozen mirrors with the same title, CRC and block count.
 * The in-game sheet only has room for one card per distinct pack, so identical
 * mirrors are collapsed here. Packs with a different block count are kept: they
 * may contain a genuinely different set of cheats.
 */
internal fun distinctCheatPacksForGame(packs: List<RemoteCheatPack>): List<RemoteCheatPack> =
    packs.distinctBy { pack ->
        "${pack.crc}|${pack.title.trim().lowercase(Locale.US)}|${pack.blockCount}"
    }

/**
 * Selects the in-game cards for the running game from the full online catalog
 * and collapses mirror duplicates. Kept as a pure function so catalog matching
 * and the in-game dedupe can be unit tested without Android dependencies.
 */
internal fun selectInGameCheatPacks(
    entries: List<RemoteCheatPack>,
    serial: String,
    crc: String,
    gameTitle: String
): RemoteCheatSelection {
    val selection = selectRemoteCheatPacks(
        index = indexRemoteCheatCatalog(entries),
        serial = serial,
        crc = crc,
        gameTitle = gameTitle
    )
    return RemoteCheatSelection(
        compatible = distinctCheatPacksForGame(selection.compatible),
        otherVersions = distinctCheatPacksForGame(selection.otherVersions)
    )
}

/** Puts the browser into the loading state for one game identity. */
internal fun CheatPackBrowserState.beginCatalogLoad(
    identityKey: String,
    serial: String,
    crc: String
): CheatPackBrowserState = copy(
    phase = CheatPackBrowserPhase.Loading,
    identityKey = identityKey,
    serial = serial,
    crc = crc,
    compatiblePacks = emptyList(),
    otherVersionPacks = emptyList(),
    workingPackId = null,
    failedPackId = null,
    importSucceeded = null,
    // Session-only "installed" badges are valid only for the same game.
    installedPackIds = if (this.identityKey == identityKey) installedPackIds else emptySet()
)

/**
 * Applies a catalog result. A null selection represents a catalog that could not
 * be loaded (offline without cache), while an empty selection is a valid answer.
 */
internal fun CheatPackBrowserState.applyCatalogSelection(
    selection: RemoteCheatSelection?
): CheatPackBrowserState {
    if (selection == null) return copy(phase = CheatPackBrowserPhase.Error)
    return copy(
        phase = if (selection.compatible.isEmpty() && selection.otherVersions.isEmpty()) {
            CheatPackBrowserPhase.Empty
        } else {
            CheatPackBrowserPhase.Content
        },
        compatiblePacks = selection.compatible,
        otherVersionPacks = selection.otherVersions
    )
}

internal fun CheatPackBrowserState.startPackInstall(packId: String): CheatPackBrowserState =
    copy(workingPackId = packId, failedPackId = null, importSucceeded = null)

internal fun CheatPackBrowserState.failPackInstall(packId: String): CheatPackBrowserState =
    copy(workingPackId = null, failedPackId = packId)

internal fun CheatPackBrowserState.startImport(): CheatPackBrowserState = copy(
    importing = true,
    importSucceeded = null,
    failedPackId = null,
    // The imported file replaces this game's PNACH, so packs installed earlier
    // in the session are no longer represented by the browser's installed set.
    installedPackIds = emptySet()
)

internal fun CheatPackBrowserState.failImport(): CheatPackBrowserState =
    copy(importing = false, importSucceeded = false)

/**
 * Records a successful install. A null [packId] means the user imported a file,
 * which replaces the game's PNACH and therefore has no catalog card to badge.
 */
internal fun CheatPackBrowserState.finishInstall(packId: String?): CheatPackBrowserState = copy(
    workingPackId = null,
    importing = false,
    failedPackId = null,
    installedPackIds = if (packId != null) installedPackIds + packId else installedPackIds,
    importSucceeded = if (packId == null) true else importSucceeded
)
