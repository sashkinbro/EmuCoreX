package com.sbro.emucorex.ui.emulation

import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.InstalledGpuDriver
import com.sbro.emucorex.core.RemoteGpuDriver
import com.sbro.emucorex.data.PerGameSettings
import com.sbro.emucorex.ui.settings.installedDriverName

internal val GPU_DRIVER_PROFILE_KEYS = setOf("gpuDriverType", "customDriverPath")

/** Which value an in-game driver change edits. */
internal enum class GpuDriverScope {
    Game,
    Global
}

/** A per-game driver write requested by the in-game Drivers tab. */
internal sealed interface GpuDriverProfileUpdate {
    /** Drop the per-game driver override so the profile inherits the app-wide driver. */
    data object UseGlobal : GpuDriverProfileUpdate

    /** Pin [path] (null = system driver) for this game. */
    data class Override(val path: String?) : GpuDriverProfileUpdate
}

internal fun PerGameSettings.withGpuDriverUpdate(update: GpuDriverProfileUpdate?): PerGameSettings =
    when (update) {
        null -> this
        GpuDriverProfileUpdate.UseGlobal -> copy(
            gpuDriverType = 0,
            customDriverPath = null,
            providedKeys = providedKeys?.minus(GPU_DRIVER_PROFILE_KEYS)
        )
        is GpuDriverProfileUpdate.Override -> copy(
            gpuDriverType = if (update.path.isNullOrBlank()) 0 else 1,
            customDriverPath = update.path?.takeIf { it.isNotBlank() },
            providedKeys = providedKeys?.plus(GPU_DRIVER_PROFILE_KEYS)
        )
    }

/** Whether [profile] pins its own driver (a null key set means every key is owned). */
internal fun PerGameSettings.ownsGpuDriver(): Boolean =
    providedKeys == null || providedKeys.any { it in GPU_DRIVER_PROFILE_KEYS }

/** A driver the user asked to apply; [path] null means the system driver. */
internal data class GpuDriverTarget(val path: String?)

/** Human identity of an installed driver, derived from its install folder name. */
internal data class GpuDriverIdentity(val name: String, val version: String?) {
    val title: String
        get() = if (version.isNullOrBlank()) name else "$name $version"
}

private val driverFolderSeparators = Regex("[-_ ]+")
private val driverVersionToken = Regex("^[vV]?\\d+(\\.\\d+)+$")
private val driverHashToken = Regex("^[0-9a-fA-F]{7,}$")

/**
 * "malisx2-0.5.1-308fe121c7" becomes "MaliSX2" / "0.5.1". The native overlay applies the same rule
 * (FriendlyCustomDriverName in GS.cpp), so both show the same name.
 */
internal fun friendlyGpuDriverIdentity(folder: String): GpuDriverIdentity {
    var version: String? = null
    val words = ArrayList<String>()
    for (token in folder.split(driverFolderSeparators).filter { it.isNotEmpty() }) {
        if (version == null && driverVersionToken.matches(token)) {
            version = token.removePrefix("v").removePrefix("V")
            continue
        }
        if (version != null || driverHashToken.matches(token)) continue
        if (token.equals("android", ignoreCase = true)) continue
        words += token
    }
    // No dotted version ("turnip-axfl3-003-android"): the tail after the name is the version tag.
    val nameWords = if (version == null && words.size > 1) {
        version = words.drop(1).joinToString("-")
        words.take(1)
    } else {
        words
    }
    val name = nameWords.joinToString(" ") { token ->
        when (token.lowercase()) {
            "malisx2" -> "MaliSX2"
            "panvk" -> "PanVK"
            else -> token.replaceFirstChar { it.uppercase() }
        }
    }.ifBlank { folder }
    return GpuDriverIdentity(name, version)
}

/** Identity of [driver]: the catalog name when it came from the catalog, otherwise parsed from the folder. */
internal fun GpuDriverBrowserState.identityOf(driver: InstalledGpuDriver): GpuDriverIdentity {
    val parsed = friendlyGpuDriverIdentity(driver.name)
    val catalogName = catalog.firstOrNull { it.installedDriverName() == driver.name }?.name
        ?.takeIf { it.isNotBlank() }
    if (catalogName == null) return parsed
    val version = parsed.version?.takeUnless { catalogName.contains(it) }
    return GpuDriverIdentity(catalogName, version)
}

internal enum class GpuDriverTabMessage {
    Applied,
    FellBackToSystem,
    StoredForVulkan,
    ApplyFailed,
    SavedGlobalGameKeeps,
    Installed,
    InstallFailed
}

internal fun gpuDriverMessageForReloadStatus(status: Int): GpuDriverTabMessage = when (status) {
    EmulatorBridge.GPU_DRIVER_RELOAD_APPLIED -> GpuDriverTabMessage.Applied
    EmulatorBridge.GPU_DRIVER_RELOAD_FELL_BACK_TO_SYSTEM -> GpuDriverTabMessage.FellBackToSystem
    EmulatorBridge.GPU_DRIVER_RELOAD_STORED_NO_VM,
    EmulatorBridge.GPU_DRIVER_RELOAD_STORED_NOT_VULKAN,
    EmulatorBridge.GPU_DRIVER_RELOAD_DEFERRED -> GpuDriverTabMessage.StoredForVulkan
    else -> GpuDriverTabMessage.ApplyFailed
}

/**
 * State of the in-game Drivers tab. It lives in the view model so a driver download survives
 * the menu being closed and the catalog is fetched only until it has loaded once.
 */
internal data class GpuDriverBrowserState(
    val catalogLoading: Boolean = false,
    val catalogLoaded: Boolean = false,
    val catalogFailed: Boolean = false,
    val installed: List<InstalledGpuDriver> = emptyList(),
    /** Compatible catalog drivers only, best recommendation first. */
    val catalog: List<RemoteGpuDriver> = emptyList(),
    /** Catalog id to download progress (0..1). */
    val downloads: Map<String, Float> = emptyMap(),
    val importing: Boolean = false,
    val applying: Boolean = false,
    /** Driver library being applied right now (null path = system driver); null when idle. */
    val applyingTarget: GpuDriverTarget? = null,
    val canUseGameScope: Boolean = false,
    val scope: GpuDriverScope = GpuDriverScope.Global,
    val scopeInitialized: Boolean = false,
    val globalPath: String? = null,
    val gameOverride: Boolean = false,
    val gamePath: String? = null,
    /** Driver libraries that failed to load and made the native side fall back to the system one. */
    val failedPaths: Set<String> = emptySet(),
    val message: GpuDriverTabMessage? = null,
    val messageArg: String? = null,
    val messageNonce: Long = 0L
) {
    /** The driver the running game actually uses (null = system). */
    val effectivePath: String?
        get() = if (canUseGameScope && gameOverride) gamePath else globalPath

    val activeScope: GpuDriverScope
        get() = if (canUseGameScope) scope else GpuDriverScope.Global

    /** The driver the selected scope currently points at (null = system). */
    val scopePath: String?
        get() = when (activeScope) {
            GpuDriverScope.Game -> if (gameOverride) gamePath else globalPath
            GpuDriverScope.Global -> globalPath
        }

    val busy: Boolean
        get() = applying || importing
}
