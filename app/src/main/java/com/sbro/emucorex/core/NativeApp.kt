package com.sbro.emucorex.core

import android.content.Context
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.DocumentsContract
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
    private const val MAX_TREE_SEARCH_ENTRIES = 8192
    private const val MAX_TREE_SEARCH_DEPTH = 16

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

    @JvmStatic external fun convertTexture(source: String, destination: String, block: Int): String?
    @JvmStatic external fun validateOptimizedTexture(path: String, block: Int): Boolean
    @JvmStatic external fun supportsAstcTextures(): Boolean

    /** Resolves an arcade manifest asset without copying multi-gigabyte CHD images. */
    @JvmStatic
    fun resolveArcadeAssetUri(manifestUri: String, relativePath: String): String? {
        val context = getContext() ?: return null
        val relative = relativePath.replace('\\', '/').trim('/')
        if (relative.isEmpty()) return null
        val resolved = runCatching {
            if (manifestUri.startsWith("content://")) {
                resolveContentArcadeAsset(context, manifestUri.toUri(), relative)
            } else {
                resolveLocalArcadeAsset(context, manifestUri, relative)
            }
        }.onFailure { error ->
            Log.w(TAG, "Unable to resolve arcade asset '$relativePath' from '$manifestUri'", error)
        }.getOrNull()
        if (resolved != null) {
            Log.d(TAG, "Resolved arcade asset '$relative' -> $resolved")
        } else {
            Log.d(TAG, "Arcade asset '$relative' was not found for manifest '$manifestUri'")
        }
        return resolved
    }

    private fun resolveLocalArcadeAsset(context: Context, manifestPath: String, relative: String): String? {
        val manifestFile = if (manifestPath.startsWith("file:")) {
            File(manifestPath.toUri().path ?: manifestPath)
        } else {
            File(manifestPath)
        }
        val baseDir = manifestFile.parentFile ?: File(manifestPath).parentFile
        if (baseDir != null) {
            val target = File(baseDir, relative).canonicalFile
            if (target.isFile) return target.absolutePath
        }

        // The library can keep a plain path after the direct file grant disappears (for
        // example when the game lives on a removable volume). Retry through a persisted SAF
        // tree so SD-card and USB arcade sets stay launchable.
        val fallbackUri = DocumentPathResolver.findAccessibleTreeUriForRawPath(context, manifestPath)
            ?: return null
        if (!DocumentsContract.isTreeUri(fallbackUri) && runCatching { DocumentsContract.getDocumentId(fallbackUri) }.isFailure) {
            return null
        }
        return resolveContentArcadeAsset(context, fallbackUri, relative)
    }

    private fun resolveContentArcadeAsset(context: Context, manifest: Uri, relative: String): String? {
        val manifestId = runCatching { DocumentsContract.getDocumentId(manifest) }.getOrNull()
            ?: runCatching { DocumentsContract.getTreeDocumentId(manifest) }.getOrNull()
            ?: return null
        val authority = manifest.authority ?: return null
        val treeUri = findTreeUriForManifest(context, manifest, manifestId)

        // Fast path: external storage and most file-manager providers expose hierarchical
        // document IDs, so the asset ID can be derived without listing directories.
        ArcadeDocumentIds.parentDocumentId(manifestId)?.let { parentId ->
            resolveArcadeAssetFromParent(context, treeUri, authority, parentId, relative)
                ?.let { return it }
        }

        // Robust path: providers with opaque document IDs (or a missing persisted grant)
        // only expose the tree itself, so walk from the granted root to the manifest parent
        // and then follow the relative asset path from there.
        if (treeUri != null) {
            locateManifestParentDocumentId(context, treeUri, manifest, manifestId)?.let { parentId ->
                resolveArcadeAssetFromParent(context, treeUri, authority, parentId, relative)
                    ?.let { return it }
            }
        }
        for (permission in context.contentResolver.persistedUriPermissions) {
            if (!permission.isReadPermission || permission.uri.authority != authority) continue
            val permissionUri = permission.uri
            if (!DocumentsContract.isTreeUri(permissionUri)) continue
            locateManifestParentDocumentId(context, permissionUri, manifest, manifestId)?.let { parentId ->
                resolveArcadeAssetFromParent(context, permissionUri, authority, parentId, relative)
                    ?.let { return it }
            }
        }
        return null
    }

    private fun findTreeUriForManifest(context: Context, manifest: Uri, manifestId: String): Uri? {
        // A document URI created by DocumentFile already embeds the picker tree, which keeps
        // every generated child URI inside the granted prefix.
        if (DocumentsContract.isTreeUri(manifest)) return manifest
        val authority = manifest.authority ?: return null

        val persistedTree = context.contentResolver.persistedUriPermissions
            .asSequence()
            .filter { it.isReadPermission && it.uri.authority == authority && DocumentsContract.isTreeUri(it.uri) }
            .filter { permission ->
                val rootId = runCatching { DocumentsContract.getTreeDocumentId(permission.uri) }.getOrNull()
                    ?: return@filter false
                manifestId == rootId ||
                    manifestId.startsWith(if (rootId.endsWith(':')) rootId else "$rootId/")
            }
            .maxByOrNull { permission ->
                runCatching { DocumentsContract.getTreeDocumentId(permission.uri) }.getOrDefault("").length
            }
            ?.uri
        if (persistedTree != null) return persistedTree

        // Without a persisted grant, root the tree at the manifest parent. Its encoded
        // document ID still carries the prefix of the original picker grant.
        val parentId = ArcadeDocumentIds.parentDocumentId(manifestId) ?: return null
        return runCatching { DocumentsContract.buildTreeDocumentUri(authority, parentId) }.getOrNull()
    }

    private fun resolveArcadeAssetFromParent(
        context: Context,
        treeUri: Uri?,
        authority: String,
        parentId: String,
        relative: String
    ): String? {
        val parts = relative.split('/').filter { it.isNotEmpty() && it != "." }
        if (parts.isEmpty()) return null
        val floorId = treeUri?.let { uri ->
            runCatching { DocumentsContract.getTreeDocumentId(uri) }.getOrNull()
        }

        var currentId: String? = parentId
        for (part in parts) {
            currentId = when (part) {
                ".." -> currentId?.let { ArcadeDocumentIds.ascendDocumentId(it, floorId) }
                else -> currentId?.let { ArcadeDocumentIds.appendDocumentId(it, part) }
            } ?: break
        }
        currentId?.let { candidate ->
            buildAssetDocumentUri(treeUri, authority, candidate)?.let { uri ->
                if (isReadableDocument(context, uri)) return uri.toString()
            }
        }

        currentId = parentId
        for (part in parts) {
            currentId = when (part) {
                ".." -> currentId?.let { ArcadeDocumentIds.ascendDocumentId(it, floorId) }
                else -> currentId?.let { findChildDocumentId(context, treeUri, authority, it, part) }
            } ?: return null
        }
        val uri = buildAssetDocumentUri(treeUri, authority, currentId) ?: return null
        return if (isReadableDocument(context, uri)) uri.toString() else null
    }

    private fun buildAssetDocumentUri(treeUri: Uri?, authority: String, documentId: String): Uri? {
        if (treeUri != null && DocumentsContract.isTreeUri(treeUri)) {
            runCatching { DocumentsContract.buildDocumentUriUsingTree(treeUri, documentId) }
                .getOrNull()
                ?.let { return it }
        }
        return runCatching { DocumentsContract.buildDocumentUri(authority, documentId) }.getOrNull()
    }

    private fun isReadableDocument(context: Context, uri: Uri): Boolean =
        runCatching {
            context.contentResolver.openFileDescriptor(uri, "r")?.use { true } ?: false
        }.getOrDefault(false)

    private fun findChildDocumentId(
        context: Context,
        treeUri: Uri?,
        authority: String,
        parentId: String,
        displayName: String
    ): String? {
        val childrenUri = when {
            treeUri != null && DocumentsContract.isTreeUri(treeUri) ->
                runCatching { DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentId) }.getOrNull()
            else ->
                runCatching { DocumentsContract.buildChildDocumentsUri(authority, parentId) }.getOrNull()
        } ?: return null
        val cursor = runCatching {
            context.contentResolver.query(
                childrenUri,
                arrayOf(
                    DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME
                ),
                null,
                null,
                null
            )
        }.getOrNull() ?: return null
        cursor.use { rows ->
            while (rows.moveToNext()) {
                val name = rows.getString(1) ?: continue
                if (name.equals(displayName, ignoreCase = true)) {
                    return rows.getString(0)
                }
            }
        }
        return null
    }

    private fun locateManifestParentDocumentId(
        context: Context,
        treeUri: Uri,
        manifestUri: Uri,
        manifestId: String
    ): String? {
        if (!DocumentsContract.isTreeUri(treeUri)) return null
        val rootId = runCatching { DocumentsContract.getTreeDocumentId(treeUri) }.getOrNull() ?: return null
        val visited = HashSet<String>()
        val queue = ArrayDeque<Pair<String, Int>>()
        queue.add(rootId to 0)
        var visitedEntries = 0
        while (queue.isNotEmpty()) {
            val (directoryId, depth) = queue.removeFirst()
            if (!visited.add(directoryId) || depth > MAX_TREE_SEARCH_DEPTH) continue
            val childrenUri = runCatching {
                DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, directoryId)
            }.getOrNull() ?: continue
            val cursor = runCatching {
                context.contentResolver.query(
                    childrenUri,
                    arrayOf(
                        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                        DocumentsContract.Document.COLUMN_MIME_TYPE
                    ),
                    null,
                    null,
                    null
                )
            }.getOrNull() ?: continue
            cursor.use { rows ->
                while (rows.moveToNext()) {
                    if (++visitedEntries > MAX_TREE_SEARCH_ENTRIES) return null
                    val childId = rows.getString(0) ?: continue
                    if (childId == manifestId) return directoryId
                    val childUri = runCatching {
                        DocumentsContract.buildDocumentUriUsingTree(treeUri, childId)
                    }.getOrNull()
                    if (childUri == manifestUri) return directoryId
                    if (rows.getString(1) == DocumentsContract.Document.MIME_TYPE_DIR) {
                        queue.add(childId to depth + 1)
                    }
                }
            }
        }
        return null
    }

    @Suppress("unused") // Exported JNI entry point retained for native data-root reloads.
    @JvmStatic external fun reloadDataRoot(path: String)
    @JvmStatic external fun setSystemCaBundlePath(path: String)
    @JvmStatic external fun getGameTitle(path: String): String?
    @JvmStatic external fun isBiosPath(path: String): Boolean
    /** Takes ownership of [fd] and always closes it before returning. */
    @JvmStatic external fun isBiosFd(fd: Int): Boolean
    @JvmStatic external fun setPerformanceMetricsEnabled(visible: Boolean, detailed: Boolean, gpuTiming: Boolean)
    @JvmStatic external fun getPerformanceMetricsSnapshot(): String?
    @JvmStatic external fun getNominalFrameRate(): Float
    @JvmStatic external fun setDisplayRefreshRate(refreshRate: Float)
    @JvmStatic external fun getDisplayDrawRect(): FloatArray?
    @JvmStatic external fun getCoreVersion(): String?
    @JvmStatic external fun queueGsDump(frames: Int)
    @JvmStatic external fun setPadButton(padIndex: Int, index: Int, range: Int, pressed: Boolean)
    @JvmStatic external fun setInternetLinkTransportReady(ready: Boolean)
    @JvmStatic external fun resetInternetLinkTransport()
    @JvmStatic external fun pushInternetLinkFrame(frame: ByteArray): Boolean
    @JvmStatic external fun pollInternetLinkFrame(): ByteArray?
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
    @JvmStatic external fun validateLosslessDll(path: String): Boolean
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
    @JvmStatic external fun onNativeSurfaceChanged(surface: Surface, width: Int, height: Int, refreshRate: Float)
    @JvmStatic external fun onNativeSurfaceDestroyed()
    @JvmStatic external fun runVMThread(path: String): Boolean
    /** Reason for the most recent native VM start failure, or null when none was recorded. */
    @JvmStatic external fun getLastBootError(): String?
    /** Hot-swaps the mounted image on the CPU thread and leaves the VM paused for the caller to resume. */
    @JvmStatic external fun changeDisc(path: String): Boolean
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
