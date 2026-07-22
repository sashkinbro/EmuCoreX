package com.sbro.emucorex.core

import android.content.Context
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Surface
import org.json.JSONArray
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.lang.ref.WeakReference
import java.security.cert.CertificateEncodingException
import java.security.cert.X509Certificate
import java.util.Base64
import java.util.zip.ZipFile
import javax.net.ssl.TrustManagerFactory
import javax.net.ssl.X509TrustManager
import androidx.core.net.toUri
import com.sbro.emucorex.core.utils.RetroAchievementsBridge
import com.sbro.emucorex.core.utils.RetroAchievementsHostOverrideReceiver

object NativeApp {

    private const val TAG = "NativeApp"
    private const val RESOURCE_ROOT = "resources"
    private const val MAX_ACTIVE_SOUND_PLAYERS = 4

    @JvmStatic
    val hasNativeTools: Boolean

    @JvmStatic
    val loadedCoreLibraryName: String

    @JvmStatic
    val hasNativeCore: Boolean

    private var contextRef: WeakReference<Context>? = null
    private var dataRootOverride: String? = null
    private val soundHandler = Handler(Looper.getMainLooper())
    private val activeSoundPlayers = LinkedHashSet<MediaPlayer>()


    init {
        loadedCoreLibraryName = AndroidNativeCoreSelector.selectedLibraryName()
        hasNativeCore = try {
            System.loadLibrary(loadedCoreLibraryName)
            Log.i(TAG, "Loaded $loadedCoreLibraryName for ${AndroidNativeCoreSelector.runtimePageSizeBytes()} byte pages")
            true
        } catch (error: UnsatisfiedLinkError) {
            Log.e(TAG, "Unable to load $loadedCoreLibraryName", error)
            CrashLogger.logError(TAG, "$loadedCoreLibraryName load FAILED", error)
            false
        }

        hasNativeTools = try {
            System.loadLibrary("EmuCoreX_native_tools")
            true
        } catch (_: UnsatisfiedLinkError) {
            false
        }
    }

    @JvmStatic external fun initialize(path: String, apiVer: Int)
    @Suppress("unused") // Exported JNI entry point retained for native data-root reloads.
    @JvmStatic external fun reloadDataRoot(path: String)
    @JvmStatic external fun setSystemCaBundlePath(path: String)
    @JvmStatic external fun getGameTitle(path: String): String?
    @JvmStatic external fun isBiosPath(path: String): Boolean
    /** Takes ownership of [fd] and always closes it before returning. */
    @JvmStatic external fun isBiosFd(fd: Int): Boolean
    @JvmStatic external fun setPerformanceMetricsEnabled(visible: Boolean, detailed: Boolean, gpuTiming: Boolean)
    @JvmStatic external fun getPerformanceMetricsSnapshot(): String?
    @JvmStatic external fun getCoreVersion(): String?
    @JvmStatic external fun queueGsDump(frames: Int)
    @JvmStatic external fun setPadButton(padIndex: Int, index: Int, range: Int, pressed: Boolean)
    @JvmStatic external fun setPadPressureModifierAmount(amountPercent: Int)
    @JvmStatic external fun onHostKeyEvent(keyCode: Int, pressed: Boolean)
    @JvmStatic external fun onHostMousePosition(x: Float, y: Float)
    @JvmStatic external fun onHostMouseButton(button: Int, pressed: Boolean)
    @JvmStatic external fun onHostMouseWheel(deltaX: Float, deltaY: Float)
    @JvmStatic external fun resetKeyStatus()
    @JvmStatic external fun resetPadState(padIndex: Int)
    @JvmStatic external fun setAspectRatio(type: Int)
    @JvmStatic external fun renderUpscalemultiplier(value: Float)
    @JvmStatic external fun getMaxUpscaleMultiplier(renderer: Int): Int
    @Suppress("unused") // Exported JNI entry point retained for renderer compatibility.
    @JvmStatic external fun renderGpu(value: Int)
    @JvmStatic external fun setCustomDriverPath(path: String)
    @JvmStatic external fun setNativeLibraryDir(path: String)
    @JvmStatic external fun beginSettingsBatch()
    @JvmStatic external fun endSettingsBatch()
    @JvmStatic external fun setSetting(section: String, key: String, type: String, value: String)
    @JvmStatic external fun getSetting(section: String, key: String, type: String): String?
    @JvmStatic external fun setFrameSkip(frames: Int)
    @JvmStatic external fun setFrameLimitEnabled(enabled: Boolean)
    @JvmStatic external fun setTurboModeEnabled(enabled: Boolean)
    @JvmStatic external fun reloadPatches()
    @JvmStatic external fun onNativeSurfaceCreated()
    @JvmStatic external fun onNativeSurfaceChanged(surface: Surface, width: Int, height: Int)
    @JvmStatic external fun onNativeSurfaceDestroyed()
    @JvmStatic external fun runVMThread(path: String): Boolean
    @JvmStatic external fun runBootSmokeProbe(path: String, steps: Int): Int
    @JvmStatic external fun runJitExecutableMemorySmokeTest(): Boolean
    @JvmStatic external fun runEeFpuDivRoundingSelfTest(): String
    @JvmStatic external fun bootElf(path: String): Boolean
    @JvmStatic external fun bootIrx(path: String): Boolean
    @JvmStatic external fun pause()
    @JvmStatic external fun resume()
    @JvmStatic external fun shutdown()
    @JvmStatic external fun refreshBIOS()
    @JvmStatic external fun hasValidVm(): Boolean
    @Suppress("unused") // Exported JNI entry point retained for native game metadata.
    @JvmStatic external fun getGameSerial(): String?
    @JvmStatic external fun saveStateToSlot(slot: Int): Boolean
    @JvmStatic external fun loadStateFromSlot(slot: Int): Boolean
    @JvmStatic external fun getSaveStatePathForFile(path: String, slot: Int): String?
    @JvmStatic external fun getSaveStateScreenshot(path: String): ByteArray?
    @JvmStatic external fun getRetroAchievementGameData(path: String): String?
    @JvmStatic external fun getRetroAchievementsAccountData(): String?
    @JvmStatic external fun setAchievementsHostOverride(host: String): Boolean
    @JvmStatic external fun clearAchievementsHostOverride(hardcoreRestoreMode: Int): Boolean
    @JvmStatic external fun listMemoryCards(): String?
    @JvmStatic external fun createMemoryCard(name: String, type: Int, fileType: Int): Boolean
    @JvmStatic external fun convertIsoToChd(inputIsoPath: String): Int
    @JvmStatic external fun startJitProfiler()
    @JvmStatic external fun stopJitProfiler()
    @JvmStatic external fun isJitProfilerActive(): Boolean
    @JvmStatic external fun startHangTrace()
    @JvmStatic external fun stopHangTrace()
    @JvmStatic external fun isHangTraceActive(): Boolean
    @JvmStatic external fun setNativeCrashLogFilePath(path: String)

    @JvmStatic
    fun parseMemoryCardList(raw: String?): List<NativeMemoryCardInfo> {
        if (raw.isNullOrBlank()) return emptyList()
        return runCatching {
            val array = JSONArray(raw)
            buildList {
                for (index in 0 until array.length()) {
                    val item = array.optJSONObject(index) ?: continue
                    add(
                        NativeMemoryCardInfo(
                            name = item.optString("name"),
                            path = item.optString("path"),
                            modifiedTime = item.optLong("modifiedTime"),
                            type = item.optInt("type"),
                            fileType = item.optInt("fileType"),
                            sizeBytes = item.optLong("sizeBytes"),
                            formatted = item.optBoolean("formatted")
                        )
                    )
                }
            }
        }.getOrDefault(emptyList())
    }

    @JvmStatic
    fun initializeOnce(context: Context) {
        contextRef = WeakReference(context.applicationContext)
        val dataRoot = resolveDataRoot(context.applicationContext)
        prepareNativeDataRoot(File(dataRoot))
        copyAssetTree(context.applicationContext, RESOURCE_ROOT, File(dataRoot, RESOURCE_ROOT))
        verifyBundledPatchArchive(File(dataRoot, "$RESOURCE_ROOT/patches.zip"))
        val caBundle = File(dataRoot, "system-ca-bundle.pem")
        exportSystemCaBundle(caBundle)
        setSystemCaBundlePath(caBundle.absolutePath)
        // Pass crash log path to native so SIGSEGV/SIGABRT are caught and written to file
        val nativeCrashLog = File(dataRoot, "logs/crash.log")
        nativeCrashLog.parentFile?.mkdirs()
        setNativeCrashLogFilePath(nativeCrashLog.absolutePath)
        initialize(dataRoot, android.os.Build.VERSION.SDK_INT)
        RetroAchievementsHostOverrideReceiver.applyAfterNativeInitialization(context.applicationContext)
    }

    @JvmStatic
    fun getContext(): Context? = contextRef?.get()

    @JvmStatic
    fun onPadVibration(index: Int, largeMotor: Float, smallMotor: Float) {
        GamepadManager.onPadVibration(index, largeMotor, smallMotor)
    }

    @JvmStatic
    @Suppress("unused") // Called from C++ with GetStaticMethodID; there is no Kotlin call site.
    fun onRetroAchievementsNotification(kind: String?, title: String?, message: String?, imagePath: String?) {
        RetroAchievementsBridge.notifyNotification(kind, title, message, imagePath)
    }

    @JvmStatic
    @Suppress("unused") // Called from C++ with GetStaticMethodID; there is no Kotlin call site.
    fun onRetroAchievementsSound(path: String?) {
        val resolvedPath = path?.takeIf { it.isNotBlank() } ?: return
        soundHandler.post { playRetroAchievementsSound(resolvedPath) }
    }

    private fun playRetroAchievementsSound(path: String) {
        if (!File(path).isFile) return

        val player = MediaPlayer()
        try {
            player.setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                    .build()
            )
            player.setDataSource(path)
            player.setOnPreparedListener { it.start() }
            player.setOnCompletionListener { releaseSoundPlayer(it) }
            player.setOnErrorListener { failedPlayer, _, _ ->
                releaseSoundPlayer(failedPlayer)
                true
            }
            while (activeSoundPlayers.size >= MAX_ACTIVE_SOUND_PLAYERS) {
                releaseSoundPlayer(activeSoundPlayers.first())
            }
            activeSoundPlayers += player
            player.prepareAsync()
        } catch (_: Exception) {
            releaseSoundPlayer(player)
        }
    }

    private fun releaseSoundPlayer(player: MediaPlayer) {
        activeSoundPlayers.remove(player)
        runCatching { player.release() }
    }

    @JvmStatic
    fun setCrashContextString(key: String, value: String?) {
        CrashLogger.logContext(key, value)
    }

    @JvmStatic
    fun setCrashContextInt(key: String, value: Int) {
        CrashLogger.logContext(key, value)
    }

    @JvmStatic
    fun setCrashContextBool(key: String, value: Boolean) {
        CrashLogger.logContext(key, value)
    }

    @JvmStatic
    fun logCrashBreadcrumb(message: String) {
        Log.i(TAG, message)
        CrashLogger.logInfo("Native", message)
    }

    @JvmStatic
    fun openContentUri(uriString: String): Int {
        val context = getContext() ?: return -1
        return try {
            val sanitized = uriString.substringBefore('|')
            val descriptor = context.contentResolver.openFileDescriptor(sanitized.toUri(), "r")
            descriptor?.detachFd() ?: -1
        } catch (_: Exception) {
            -1
        }
    }


    private fun resolveDataRoot(context: Context): String {
        val override = dataRootOverride
        if (!override.isNullOrBlank()) {
            val dir = File(override)
            if (prepareNativeDataRoot(dir)) {
                return dir.absolutePath
            }
            Log.w(TAG, "Configured data root is not writable, falling back to app internal files: $override")
        }

        val external = context.getExternalFilesDir(null)
        if (external != null && prepareNativeDataRoot(external)) {
            return external.absolutePath
        }

        val internal = context.filesDir
        prepareNativeDataRoot(internal)
        return internal.absolutePath
    }

    private fun prepareNativeDataRoot(root: File): Boolean {
        return runCatching {
            if (!root.exists() && !root.mkdirs()) {
                return@runCatching false
            }

            val requiredDirectories = arrayOf(
                File(root, "cache"),
                File(root, "cache/achievement_images"),
                File(root, "resources"),
                File(root, "inis"),
                File(root, "sstates"),
                File(root, "memcards")
            )
            requiredDirectories.forEach { dir ->
                if (!dir.exists() && !dir.mkdirs()) {
                    return@runCatching false
                }
            }

            val probe = File(root, ".native-write-probe")
            probe.writeText("ok")
            probe.delete()
            true
        }.getOrElse { error ->
            Log.w(TAG, "Native data root is not writable: ${root.absolutePath}", error)
            false
        }
    }

    private fun copyAssetTree(context: Context, assetPath: String, target: File) {
        try {
            val children = context.assets.list(assetPath) ?: emptyArray()
            if (children.isEmpty()) {
                copyAssetFile(context, assetPath, target)
                return
            }

            if (!target.exists()) {
                target.mkdirs()
            }

            children.forEach { child ->
                val childAssetPath = if (assetPath.isBlank()) child else "$assetPath/$child"
                copyAssetTree(context, childAssetPath, File(target, child))
            }
        } catch (error: IOException) {
            Log.w(TAG, "Failed to copy assets from $assetPath", error)
        }
    }

    private fun copyAssetFile(context: Context, assetPath: String, target: File) {
        try {
            target.parentFile?.mkdirs()
            context.assets.open(assetPath).use { input ->
                FileOutputStream(target).use { output ->
                    input.copyTo(output)
                }
            }
        } catch (error: IOException) {
            Log.w(TAG, "Failed to copy asset file $assetPath", error)
        }
    }

    private fun verifyBundledPatchArchive(archive: File) {
        runCatching {
            require(archive.isFile && archive.length() > 0L) { "Bundled patches.zip is missing" }
            val patchCount = ZipFile(archive).use { zip ->
                zip.entries().asSequence().count { entry ->
                    !entry.isDirectory && entry.name.endsWith(".pnach", ignoreCase = true)
                }
            }
            require(patchCount > 0) { "Bundled patches.zip contains no PNACH files" }
            Log.i(TAG, "Bundled PCSX2 patch archive ready: $patchCount patches, ${archive.length()} bytes")
        }.onFailure { error ->
            Log.e(TAG, "Bundled PCSX2 patch archive is unavailable", error)
            CrashLogger.logError(TAG, "Bundled PCSX2 patch archive unavailable", error)
        }
    }

    private fun exportSystemCaBundle(target: File) {
        try {
            target.parentFile?.mkdirs()

            val factory = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm())
            factory.init(null as java.security.KeyStore?)
            val trustManager = factory.trustManagers.filterIsInstance<X509TrustManager>().firstOrNull() ?: return

            val pemBundle = buildString {
                trustManager.acceptedIssuers
                    .distinctBy { certificateFingerprint(it) }
                    .forEachIndexed { index, certificate ->
                        if (index > 0) append('\n')
                        append(certificate.toPemBlock())
                    }
            }

            if (pemBundle.isBlank()) return
            if (target.exists() && runCatching { target.readText() }.getOrNull() == pemBundle) return
            target.writeText(pemBundle)
        } catch (error: Exception) {
            Log.w(TAG, "Failed to export Android CA bundle", error)
        }
    }

    private fun certificateFingerprint(certificate: X509Certificate): String =
        runCatching { certificate.encoded.joinToString(separator = "") { "%02x".format(it) } }
            .getOrDefault(certificate.subjectX500Principal.name)

    private fun X509Certificate.toPemBlock(): String {
        val encoded = try {
            encoded
        } catch (_: CertificateEncodingException) {
            return ""
        }
        val base64 = Base64.getMimeEncoder(64, "\n".toByteArray()).encodeToString(encoded)
        return buildString {
            appendLine("-----BEGIN CERTIFICATE-----")
            appendLine(base64)
            append("-----END CERTIFICATE-----")
        }
    }
}

data class NativeMemoryCardInfo(
    val name: String,
    val path: String,
    val modifiedTime: Long,
    val type: Int,
    val fileType: Int,
    val sizeBytes: Long,
    val formatted: Boolean
)
