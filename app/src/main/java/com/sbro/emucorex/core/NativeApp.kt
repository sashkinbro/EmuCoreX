package com.sbro.emucorex.core

import android.content.Context
import android.util.Log
import android.view.Surface
import com.google.firebase.crashlytics.FirebaseCrashlytics
import org.json.JSONArray
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.lang.ref.WeakReference
import androidx.core.net.toUri

object NativeApp {

    private const val TAG = "NativeApp"
    private const val RESOURCE_ROOT = "resources"

    @JvmStatic
    val hasNativeTools: Boolean

    private var contextRef: WeakReference<Context>? = null
    private var dataRootOverride: String? = null
    private var performanceListener: PerformanceListener? = null

    interface PerformanceListener {
        fun onMetricsUpdate(
            fps: Float,
            speedPercent: Float,
            frameTime: Float,
            cpuLoad: Float,
            gpuLoad: Float
        )
    }

    init {
        try {
            System.loadLibrary("emucore")
        } catch (_: UnsatisfiedLinkError) {
        }

        hasNativeTools = try {
            System.loadLibrary("EmuCoreX_native_tools")
            true
        } catch (_: UnsatisfiedLinkError) {
            false
        }
    }

    @JvmStatic external fun initialize(path: String, apiVer: Int)
    @JvmStatic external fun reloadDataRoot(path: String)
    @JvmStatic external fun getGameTitle(path: String): String?
    @JvmStatic external fun setPadVibration(enabled: Boolean)
    @JvmStatic external fun setPadButton(index: Int, range: Int, pressed: Boolean)
    @JvmStatic external fun resetKeyStatus()
    @JvmStatic external fun setAspectRatio(type: Int)
    @JvmStatic external fun renderUpscalemultiplier(value: Float)
    @JvmStatic external fun renderGpu(value: Int)
    @JvmStatic external fun setCustomDriverPath(path: String)
    @JvmStatic external fun setNativeLibraryDir(path: String)
    @JvmStatic external fun setSetting(section: String, key: String, type: String, value: String)
    @JvmStatic external fun applyRuntimeOperations(payload: String)
    @JvmStatic external fun getSetting(section: String, key: String, type: String): String?
    @JvmStatic external fun onNativeSurfaceCreated()
    @JvmStatic external fun onNativeSurfaceChanged(surface: Surface, width: Int, height: Int)
    @JvmStatic external fun onNativeSurfaceDestroyed()
    @JvmStatic external fun runVMThread(path: String): Boolean
    @JvmStatic external fun pause()
    @JvmStatic external fun resume()
    @JvmStatic external fun shutdown()
    @JvmStatic external fun refreshBIOS()
    @JvmStatic external fun hasValidVm(): Boolean
    @JvmStatic external fun saveStateToSlot(slot: Int): Boolean
    @JvmStatic external fun loadStateFromSlot(slot: Int): Boolean
    @JvmStatic external fun getSaveStatePathForFile(path: String, slot: Int): String?
    @JvmStatic external fun getSaveStateScreenshot(path: String): ByteArray?
    @JvmStatic external fun getRetroAchievementGameData(path: String): String?
    @JvmStatic external fun listMemoryCards(): String?
    @JvmStatic external fun createMemoryCard(name: String, type: Int, fileType: Int): Boolean
    @JvmStatic external fun convertIsoToChd(inputIsoPath: String): Int

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
        copyAssetTree(context.applicationContext, RESOURCE_ROOT, File(dataRoot, RESOURCE_ROOT))
        initialize(dataRoot, android.os.Build.VERSION.SDK_INT)
    }

    @JvmStatic
    fun getContext(): Context? = contextRef?.get()

    @JvmStatic
    fun onPadVibration(index: Int, largeMotor: Float, smallMotor: Float) {
        Log.d(TAG, "Pad vibration index=$index large=$largeMotor small=$smallMotor")
    }

    @JvmStatic
    fun onPerformanceMetrics(
        fps: Float,
        speedPercent: Float,
        frameTime: Float,
        cpuLoad: Float,
        gpuLoad: Float
    ) {
        performanceListener?.onMetricsUpdate(fps, speedPercent, frameTime, cpuLoad, gpuLoad)
    }

    @JvmStatic
    fun setPerformanceListener(listener: PerformanceListener?) {
        performanceListener = listener
    }

    @JvmStatic
    fun nativeLog(message: String) {
        Log.d("NativeCore", message)
        withCrashlytics { it.log("Native: $message") }
    }

    @JvmStatic
    fun setCrashContextString(key: String, value: String?) {
        withCrashlytics { it.setCustomKey(key, value.orEmpty()) }
    }

    @JvmStatic
    fun setCrashContextInt(key: String, value: Int) {
        withCrashlytics { it.setCustomKey(key, value) }
    }

    @JvmStatic
    fun setCrashContextBool(key: String, value: Boolean) {
        withCrashlytics { it.setCustomKey(key, value) }
    }

    @JvmStatic
    fun logCrashBreadcrumb(message: String) {
        withCrashlytics { it.log(message) }
    }

    private fun withCrashlytics(block: (FirebaseCrashlytics) -> Unit) {
        try {
            block(FirebaseCrashlytics.getInstance())
        } catch (_: Exception) { }
    }

    @JvmStatic
    fun ensureResourceSubdirectoryCopied(relativePath: String) {
        val context = getContext() ?: return
        val cleanRelativePath = relativePath.trim('/').trim()
        val assetPath = if (cleanRelativePath.isBlank()) RESOURCE_ROOT else "$RESOURCE_ROOT/$cleanRelativePath"
        val targetRoot = File(resolveDataRoot(context), assetPath)
        copyAssetTree(context, assetPath, targetRoot)
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
            if (!dir.exists()) {
                dir.mkdirs()
            }
            return dir.absolutePath
        }

        val external = context.getExternalFilesDir(null)
        if (external != null) {
            return external.absolutePath
        }

        return context.dataDir.absolutePath
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
