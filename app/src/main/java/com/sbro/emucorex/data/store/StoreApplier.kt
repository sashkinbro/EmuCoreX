package com.sbro.emucorex.data.store

import android.content.Context
import android.graphics.Color
import com.sbro.emucorex.BuildConfig
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.core.utils.RetroAchievementsStateManager
import com.sbro.emucorex.data.CustomFontRepository
import com.sbro.emucorex.data.CustomTouchControlLibrary
import com.sbro.emucorex.data.HomeBackgroundRepository
import com.sbro.emucorex.data.HomeBackgroundType
import com.sbro.emucorex.data.TouchControlPressEffect
import com.sbro.emucorex.data.UiSoundKind
import com.sbro.emucorex.data.theme.ThemePackageRepository
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.withContext
import java.io.File
import kotlin.math.roundToInt

enum class StoreApplyResult { APPLIED, NEEDS_PRO, NOT_INSTALLED, UNSUPPORTED, FAILED }

/** An installed Store controls skin, resolved to files. Rendering is up to the touch-overlay UI. */
data class ControlSkin(
    val dir: File,
    /** control id -> image. */
    val images: Map<String, File>,
    /** control id -> image shown while pressed (from the "<id>_pressed" entries). */
    val pressed: Map<String, File>,
    /** ARGB tint, or null for none. */
    val tint: Int?,
    /** 0..1. */
    val opacity: Float,
    /** Null = keep the user's press-effect preference. */
    val pressEffect: TouchControlPressEffect?
)

object StoreControlSkins {
    /** Loads the skin of installed controls package [packId]; null if missing, layout-only or unreadable. */
    fun load(context: Context, packId: String?): ControlSkin? {
        if (packId.isNullOrBlank() || !StoreInstalls.ID_PATTERN.matches(packId)) return null
        val dir = File(StoreInstalls.kindDirectory(context, StoreKind.CONTROLS), packId)
        return runCatching {
            val manifest = StoreInstalls.JSON.decodeFromString<ControlsManifest>(
                File(dir, StoreInstalls.CONTROLS_JSON).readText()
            )
            val skin = manifest.skin?.takeIf { it.images.isNotEmpty() } ?: return null
            val images = LinkedHashMap<String, File>()
            val pressed = LinkedHashMap<String, File>()
            skin.images.forEach { (controlId, name) ->
                val file = File(dir, name).takeIf { it.isFile && it.parentFile == dir } ?: return@forEach
                if (controlId.endsWith(PRESSED_SUFFIX)) {
                    pressed[controlId.removeSuffix(PRESSED_SUFFIX)] = file
                } else {
                    images[controlId] = file
                }
            }
            if (images.isEmpty()) return null
            ControlSkin(
                dir = dir,
                images = images,
                pressed = pressed,
                tint = skin.tint?.let { runCatching { Color.parseColor(it) }.getOrNull() },
                opacity = skin.opacity.coerceIn(0f, 1f),
                pressEffect = skin.pressEffect?.let { name ->
                    TouchControlPressEffect.entries.firstOrNull { it.name.equals(name, ignoreCase = true) }
                }
            )
        }.getOrNull()
    }

    private const val PRESSED_SUFFIX = "_pressed"
}

/**
 * Applies installed Store items through the existing preference/repository paths, and removes them.
 * No UI. Pro items are refused without Pro, like the built-in Pro themes.
 */
class StoreApplier(context: Context) {
    private val appContext = context.applicationContext
    private val preferences = AppPreferences(appContext)
    private val installs = StoreInstalls.getInstance(appContext)

    suspend fun apply(item: StoreItem): StoreApplyResult = withContext(Dispatchers.IO) {
        try {
            val installed = installs.find(item.id)?.takeIf { it.kind == item.kind }
                ?: return@withContext StoreApplyResult.NOT_INSTALLED
            if (item.minAppVersionCode > BuildConfig.VERSION_CODE) return@withContext StoreApplyResult.UNSUPPORTED
            if (item.requiresPro && !preferences.proUnlocked.first()) return@withContext StoreApplyResult.NEEDS_PRO
            when (item.kind) {
                StoreKind.THEME -> applyTheme(item, installed)
                StoreKind.WALLPAPER -> applyWallpaper(installed)
                StoreKind.SOUNDS -> applySounds(item, installed)
                StoreKind.CONTROLS -> applyControls(installed)
                StoreKind.FONT -> applyFont(installed)
            }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (error: Exception) {
            StoreApplyResult.FAILED
        }
    }

    /** Deletes the installed files and resets anything that pointed at them (applied theme, controls skin). */
    suspend fun remove(id: String): Boolean {
        val installed = installs.find(id) ?: return false
        when (installed.kind) {
            StoreKind.THEME -> preferences.clearThemePackage(id)
            StoreKind.CONTROLS -> if (preferences.touchControlSkinPack.first() == id) preferences.setTouchControlSkinPack(null)
            // Sound packs fall back to the default preset by themselves; the wallpaper, an applied font and an
            // applied achievement sound are independent copies.
            StoreKind.SOUNDS, StoreKind.WALLPAPER, StoreKind.FONT -> Unit
        }
        return installs.remove(id)
    }

    private suspend fun applyTheme(item: StoreItem, installed: InstalledStoreItem): StoreApplyResult {
        val pack = ThemePackageRepository.getInstance(appContext).find(installed.id)
            ?: return StoreApplyResult.NOT_INSTALLED
        if (!pack.isSupported) return StoreApplyResult.UNSUPPORTED
        val requiresPro = item.requiresPro || pack.spec.requiresPro
        return if (preferences.applyThemePackage(pack.id, requiresPro)) StoreApplyResult.APPLIED else StoreApplyResult.NEEDS_PRO
    }

    private suspend fun applyWallpaper(installed: InstalledStoreItem): StoreApplyResult {
        val manifest = StoreInstalls.JSON.decodeFromString<WallpaperManifest>(
            File(installed.directory, StoreInstalls.WALLPAPER_JSON).readText()
        )
        val image = StorePackageInstaller.WALLPAPER_IMAGES.map { File(installed.directory, it) }.firstOrNull(File::isFile)
            ?: return StoreApplyResult.NOT_INSTALLED
        HomeBackgroundRepository(appContext).install(image, HomeBackgroundType.IMAGE).getOrElse { return StoreApplyResult.FAILED }
        preferences.applyStoreWallpaper((manifest.dim * 100f).roundToInt().coerceIn(0, 85))
        return StoreApplyResult.APPLIED
    }

    /** Switches only the sound kinds the pack actually contains; a pack without a kind leaves that kind alone. */
    private suspend fun applySounds(item: StoreItem, installed: InstalledStoreItem): StoreApplyResult {
        val contents = StoreSoundPacks.contents(appContext, installed.id)
        if (contents.isEmpty()) return StoreApplyResult.NOT_INSTALLED
        val kinds = buildSet {
            if ("click" in contents) add(UiSoundKind.BUTTON_CLICK)
            if ("menu" in contents) add(UiSoundKind.GAME_MENU)
            if ("music" in contents) add(UiSoundKind.BACKGROUND_MUSIC)
        }
        if (kinds.isNotEmpty()) preferences.applyUiSoundPack(installed.id, kinds)
        if ("achievement" in contents) {
            val source = StoreSoundPacks.file(appContext, installed.id, "achievement")
                ?: return StoreApplyResult.NOT_INSTALLED
            // A private copy next to the user-imported unlock sound, so it survives removing the pack.
            val directory = File(appContext.filesDir, "achievements").apply { mkdirs() }
            val target = File(directory, "unlock_sound.${source.extension}")
            File(directory, "unlock_sound.wav").takeIf { it != target }?.delete()
            File(directory, "unlock_sound.ogg").takeIf { it != target }?.delete()
            source.copyTo(target, overwrite = true)
            RetroAchievementsStateManager.setUnlockSound(target.absolutePath, item.displayName("en"))
        }
        return StoreApplyResult.APPLIED
    }

    private suspend fun applyFont(installed: InstalledStoreItem): StoreApplyResult {
        val manifest = StoreInstalls.JSON.decodeFromString<FontManifest>(
            File(installed.directory, StoreInstalls.FONT_JSON).readText()
        )
        val font = StoreFontPacks.file(appContext, installed.id) ?: return StoreApplyResult.NOT_INSTALLED
        val result = CustomFontRepository(appContext).installFromFile(font, manifest.family)
        val custom = result.getOrElse { return StoreApplyResult.FAILED }
        preferences.setCustomFontInstalled(custom.displayName, storeFontId = installed.id)
        return StoreApplyResult.APPLIED
    }

    private suspend fun applyControls(installed: InstalledStoreItem): StoreApplyResult {
        val manifest = StoreInstalls.JSON.decodeFromString<ControlsManifest>(
            File(installed.directory, StoreInstalls.CONTROLS_JSON).readText()
        )
        val hasSkin = manifest.skin?.images?.isNotEmpty() == true
        val custom = manifest.layout?.custom?.let { CustomTouchControlLibrary.decodeOrNull(it.toString()) }
        preferences.applyStoreControls(
            // A layout-only package leaves the current skin alone.
            skinPackId = if (hasSkin) installed.id else null,
            overlayLayoutJson = manifest.layout?.overlay?.toString(),
            custom = custom
        )
        return StoreApplyResult.APPLIED
    }
}
