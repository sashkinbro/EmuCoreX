package com.sbro.emucorex.core

import android.content.Context
import android.net.Uri
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.BufferedInputStream
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.io.RandomAccessFile
import java.security.MessageDigest

data class FrameGenerationSettings(
    val enabled: Boolean = false,
    val multiplier: Int = 2,
    val performanceMode: Boolean = true,
    val flowScalePercent: Int = 100,
    val targetRefreshRate: Int = 0
)

data class FrameGenerationSetup(
    val hardwareSupported: Boolean,
    val componentInstalled: Boolean,
    val componentVersion: Int,
    val dllPath: String?,
    val dllSha256: String?,
    val settings: FrameGenerationSettings
) {
    val dllInstalled: Boolean get() = !dllPath.isNullOrBlank() && File(dllPath).isFile
    val isReady: Boolean get() = hardwareSupported && componentInstalled && dllInstalled
}

/**
 * Owns the global LSFG setup. The open-source integration is part of EmuCoreX;
 * the small remote support manifest is a revocable compatibility gate, while
 * Lossless.dll is always supplied by the user and copied into private storage.
 */
class FrameGenerationManager(context: Context) {
    private val appContext = context.applicationContext
    private val prefs = appContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
    private val root = File(appContext.filesDir, "frame-generation")
    private val manifestFile = File(root, "support.json")
    private val dllFile = File(root, "Lossless.dll")

    fun snapshot(): FrameGenerationSetup {
        val recordedPath = prefs.getString(KEY_DLL_PATH, null)
        return FrameGenerationSetup(
            hardwareSupported = supportsCurrentHardware(),
            componentInstalled = manifestFile.isFile && prefs.getInt(KEY_COMPONENT_VERSION, 0) > 0,
            componentVersion = prefs.getInt(KEY_COMPONENT_VERSION, 0),
            dllPath = recordedPath?.takeIf { File(it).isFile },
            dllSha256 = prefs.getString(KEY_DLL_SHA256, null),
            settings = FrameGenerationSettings(
                enabled = prefs.getBoolean(KEY_ENABLED, false),
                multiplier = prefs.getInt(KEY_MULTIPLIER, 2).coerceIn(2, 4),
                performanceMode = prefs.getBoolean(KEY_PERFORMANCE, true),
                flowScalePercent = normalizeFlowScale(prefs.getInt(KEY_FLOW_SCALE, 100)),
                targetRefreshRate = prefs.getInt(KEY_TARGET_RATE, 0).coerceIn(0, 240)
            )
        )
    }

    suspend fun installSupportComponent(): Result<Int> = withContext(Dispatchers.IO) {
        runCatching {
            root.mkdirs()
            val bytes = downloadManifest()
            val json = JSONObject(bytes.decodeToString())
            require(json.optInt("schema") == SUPPORT_SCHEMA) { "Unsupported support manifest schema" }
            require(json.optString("id") == SUPPORT_ID) { "Unexpected support component" }
            val version = json.optInt("version")
            require(version > 0) { "Invalid support component version" }

            val temporary = File(root, "support.json.part")
            temporary.outputStream().use { it.write(bytes) }
            check(temporary.renameTo(manifestFile) || temporary.copyTo(manifestFile, overwrite = true).let { temporary.delete(); true })
            prefs.edit().putInt(KEY_COMPONENT_VERSION, version).apply()
            version
        }
    }

    suspend fun importLosslessDll(uri: Uri): Result<String> = withContext(Dispatchers.IO) {
        runCatching {
            root.mkdirs()
            val temporary = File(root, "Lossless.dll.part")
            appContext.contentResolver.openInputStream(uri)?.use { input ->
                FileOutputStream(temporary).use { output ->
                    val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                    var total = 0L
                    while (true) {
                        val read = input.read(buffer)
                        if (read < 0) break
                        total += read
                        require(total <= MAX_DLL_BYTES) { "Selected file is too large" }
                        output.write(buffer, 0, read)
                    }
                }
            } ?: error("Could not open selected file")

            require(temporary.length() >= MIN_DLL_BYTES) { "Selected file is not Lossless.dll" }
            require(hasPeSignature(temporary)) { "Selected file is not a Windows DLL" }
            require(runCatching { NativeApp.validateLosslessDll(temporary.absolutePath) }.getOrDefault(false)) {
                "The DLL does not contain a readable PE image"
            }
            val digest = sha256(temporary)
            check(temporary.renameTo(dllFile) || temporary.copyTo(dllFile, overwrite = true).let { temporary.delete(); true })
            prefs.edit()
                .putString(KEY_DLL_PATH, dllFile.absolutePath)
                .putString(KEY_DLL_SHA256, digest)
                .apply()
            applyToNative(snapshot())
            dllFile.absolutePath
        }
    }

    fun updateSettings(value: FrameGenerationSettings) {
        val normalized = value.copy(
            enabled = value.enabled && snapshot().isReady,
            multiplier = value.multiplier.coerceIn(2, 4),
            flowScalePercent = normalizeFlowScale(value.flowScalePercent),
            targetRefreshRate = value.targetRefreshRate.coerceIn(0, 240)
        )
        prefs.edit()
            .putBoolean(KEY_ENABLED, normalized.enabled)
            .putInt(KEY_MULTIPLIER, normalized.multiplier)
            .putBoolean(KEY_PERFORMANCE, normalized.performanceMode)
            .putInt(KEY_FLOW_SCALE, normalized.flowScalePercent)
            .putInt(KEY_TARGET_RATE, normalized.targetRefreshRate)
            .apply()
        applyToNative(snapshot())
    }

    fun applyToNative(setup: FrameGenerationSetup = snapshot(), enabledOverride: Boolean? = null) {
        applySettingsToNative(setup.settings.copy(enabled = enabledOverride ?: setup.settings.enabled), setup)
    }

    fun applySettingsToNative(settings: FrameGenerationSettings, setup: FrameGenerationSetup = snapshot()) {
        val enabled = settings.enabled && setup.isReady
        runCatching {
            NativeApp.beginSettingsBatch()
            NativeApp.setSetting(SECTION, "LsfgDllPath", "string", setup.dllPath.orEmpty())
            NativeApp.setSetting(SECTION, "LsfgMultiplier", "int", settings.multiplier.coerceIn(2, 4).toString())
            NativeApp.setSetting(SECTION, "LsfgPerformance", "bool", settings.performanceMode.toString())
            NativeApp.setSetting(SECTION, "LsfgFlowScale", "int", normalizeFlowScale(settings.flowScalePercent).toString())
            NativeApp.setSetting(SECTION, "LsfgTargetRate", "int", settings.targetRefreshRate.coerceIn(0, 240).toString())
            NativeApp.setSetting(SECTION, "LsfgEnabled", "bool", enabled.toString())
            NativeApp.endSettingsBatch()
        }.onFailure { runCatching { NativeApp.endSettingsBatch() } }
    }

    private fun downloadManifest(): ByteArray {
        var lastError: Throwable? = null
        for (endpoint in SUPPORT_ENDPOINTS) {
            try {
                val connection = URL(endpoint).openConnection() as HttpURLConnection
                connection.connectTimeout = 15_000
                connection.readTimeout = 20_000
                connection.instanceFollowRedirects = true
                connection.setRequestProperty("Accept", "application/json")
                connection.setRequestProperty("User-Agent", "EmuCoreX-FrameGeneration")
                try {
                    require(connection.responseCode in 200..299) { "HTTP ${connection.responseCode}" }
                    return BufferedInputStream(connection.inputStream).use { input ->
                        val output = java.io.ByteArrayOutputStream()
                        val buffer = ByteArray(4096)
                        while (true) {
                            val read = input.read(buffer)
                            if (read < 0) break
                            require(output.size() + read <= MAX_MANIFEST_BYTES) { "Support manifest is too large" }
                            output.write(buffer, 0, read)
                        }
                        output.toByteArray()
                    }
                } finally {
                    connection.disconnect()
                }
            } catch (error: Throwable) {
                lastError = error
            }
        }
        throw lastError ?: IllegalStateException("No support endpoint configured")
    }

    companion object {
        const val STEAM_URL = "https://store.steampowered.com/app/993090/Lossless_Scaling/"
        internal const val SUPPORT_ID = "emucorex-lsfg-support"
        internal const val SUPPORT_SCHEMA = 1
        private const val PREFS_NAME = "frame_generation"
        private const val SECTION = "EmuCore/GS"
        private const val KEY_COMPONENT_VERSION = "component_version"
        private const val KEY_DLL_PATH = "dll_path"
        private const val KEY_DLL_SHA256 = "dll_sha256"
        private const val KEY_ENABLED = "enabled"
        private const val KEY_MULTIPLIER = "multiplier"
        private const val KEY_PERFORMANCE = "performance"
        private const val KEY_FLOW_SCALE = "flow_scale"
        private const val KEY_TARGET_RATE = "target_rate"
        private const val MAX_MANIFEST_BYTES = 64 * 1024
        private const val MIN_DLL_BYTES = 1024L * 1024L
        private const val MAX_DLL_BYTES = 256L * 1024L * 1024L
        private val SUPPORT_ENDPOINTS = listOf(
            "https://raw.githubusercontent.com/sashkinbro/EmuCoreX-Cheat/main/frame-generation/support-v1.json",
            "https://cdn.jsdelivr.net/gh/sashkinbro/EmuCoreX-Cheat@main/frame-generation/support-v1.json"
        )

        internal fun normalizeFlowScale(value: Int): Int =
            ((value.coerceIn(25, 100) + 12) / 25) * 25

        fun supportsCurrentHardware(): Boolean =
            supportsAdrenoFamily(GpuDriverRecommendations.currentDeviceProfile()?.family)

        internal fun supportsAdrenoFamily(family: AdrenoFamily?): Boolean =
            family == AdrenoFamily.A7XX || family == AdrenoFamily.A8XX

        internal fun hasPeSignature(file: File): Boolean = runCatching {
            if (file.length() < 0x40L) return@runCatching false
            RandomAccessFile(file, "r").use { input ->
                if (input.readUnsignedByte() != 'M'.code || input.readUnsignedByte() != 'Z'.code) {
                    return@use false
                }
                input.seek(0x3c)
                val peOffset = Integer.toUnsignedLong(Integer.reverseBytes(input.readInt()))
                if (peOffset < 0x40L || peOffset > file.length() - 4L) return@use false
                input.seek(peOffset)
                input.readUnsignedByte() == 'P'.code &&
                    input.readUnsignedByte() == 'E'.code &&
                    input.readUnsignedByte() == 0 &&
                    input.readUnsignedByte() == 0
            }
        }.getOrDefault(false)

        internal fun sha256(file: File): String {
            val digest = MessageDigest.getInstance("SHA-256")
            file.inputStream().buffered().use { input ->
                val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                while (true) {
                    val read = input.read(buffer)
                    if (read < 0) break
                    digest.update(buffer, 0, read)
                }
            }
            return digest.digest().joinToString("") { "%02x".format(it) }
        }
    }
}
