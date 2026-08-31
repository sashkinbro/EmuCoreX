package com.sbro.emucorex.data

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import com.sbro.emucorex.core.EmulatorStorage
import com.sbro.emucorex.core.NativeApp
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityRepository
import java.io.File
import java.io.InputStream
import java.util.Locale
import java.util.UUID
import java.util.zip.ZipInputStream
import kotlinx.coroutines.flow.first

data class TexturePackInfo(
    val serial: String,
    val gameTitle: String?,
    val replacementCount: Int,
    val dumpCount: Int,
    val sizeBytes: Long,
    val lastModifiedAt: Long,
    val canOptimize: Boolean = true
)

data class TexturePackSummary(
    val rootPath: String,
    val packs: List<TexturePackInfo>,
    val totalReplacementCount: Int,
    val totalDumpCount: Int,
    val totalSizeBytes: Long
)

data class TextureImportResult(
    val success: Boolean,
    val importedFiles: Int = 0,
    val importedSerials: Set<String> = emptySet(),
    val optimizedFiles: Int = 0,
    val skippedFiles: Int = 0,
    val error: String = ""
)

data class TextureOptimizationProgress(
    val completed: Int,
    val total: Int,
    val optimized: Int,
    val skipped: Int,
    val currentName: String
)

class TexturePackRepository(
    private val context: Context,
    private val preferences: AppPreferences,
    private val rootOverride: File? = null
) {
    private val textureExtensions = setOf("png", "dds", "ktx2")
    private val serialPattern = Regex("[A-Z]{4}[-_ ]?\\d{5}", RegexOption.IGNORE_CASE)
    private val compatibilityRepository = Pcsx2CompatibilityRepository(context.applicationContext)
    private val libraryCacheRepository = GameLibraryCacheRepository(context.applicationContext)

    suspend fun listPacks(): TexturePackSummary {
        val root = texturesRoot()
        val libraryTitles = loadLibraryTitlesBySerial()
        val packs = root.listFiles()
            ?.asSequence()
            ?.filter { it.isDirectory }
            ?.mapNotNull { folder -> buildPackInfo(folder, libraryTitles) }
            ?.sortedWith(
                compareBy<TexturePackInfo> { (it.gameTitle ?: it.serial).lowercase(Locale.US) }
                    .thenBy { it.serial }
            )
            ?.toList()
            .orEmpty()

        return TexturePackSummary(
            rootPath = root.absolutePath,
            packs = packs,
            totalReplacementCount = packs.sumOf { it.replacementCount },
            totalDumpCount = packs.sumOf { it.dumpCount },
            totalSizeBytes = packs.sumOf { it.sizeBytes }
        )
    }

    fun importPackZip(
        uri: Uri,
        astcBlock: Int = -1,
        operationId: String = UUID.randomUUID().toString(),
        progress: (TextureOptimizationProgress) -> Unit = {}
    ): TextureImportResult {
        val displayName = displayName(uri)
        val input = runCatching { context.contentResolver.openInputStream(uri) }.getOrNull()
            ?: return TextureImportResult(success = false)
        return input.use { importPackZip(it, displayName, astcBlock = astcBlock, operationId = operationId, progress = progress) }
    }

    fun installRemotePack(
        archive: File,
        targetSerial: String,
        astcBlock: Int = -1,
        operationId: String = UUID.randomUUID().toString(),
        progress: (TextureOptimizationProgress) -> Unit = {}
    ): TextureImportResult {
        if (!archive.isFile) return TextureImportResult(success = false)
        return archive.inputStream().use { input ->
            importPackZip(
                input = input,
                displayName = archive.name,
                targetSerial = targetSerial,
                replaceExisting = true,
                astcBlock = astcBlock,
                operationId = operationId,
                progress = progress
            )
        }
    }

    internal fun importPackZip(
        input: InputStream,
        displayName: String?,
        targetSerial: String? = null,
        replaceExisting: Boolean = false,
        astcBlock: Int = -1,
        operationId: String = UUID.randomUUID().toString(),
        progress: (TextureOptimizationProgress) -> Unit = {}
    ): TextureImportResult {
        require(operationId.matches(Regex("[A-Za-z0-9_-]{1,128}"))) { "Invalid texture operation ID" }
        val normalizedTargetSerial = targetSerial?.let(::normalizeSerial)
        if (targetSerial != null && normalizedTargetSerial == null) return TextureImportResult(success = false)
        val stagingRoot = File(texturesRoot(), ".texture-import-$operationId")
        val fallbackSerial = findSerial(displayName)
        val importedSerials = linkedSetOf<String>()
        val stagedFiles = linkedSetOf<String>()
        val importedNames = hashSetOf<String>()
        var entryCount = 0
        var totalBytes = 0L

        var optimization = TextureOptimizationProgress(0, 0, 0, 0, "")
        var complete = false
        return try {
            stagingRoot.mkdirs()
            val canonicalStagingRoot = stagingRoot.canonicalFile
            ZipInputStream(input.buffered()).use { zip ->
                while (true) {
                    val entry = zip.nextEntry ?: break
                    try {
                        progress(TextureOptimizationProgress(0, 0, 0, 0, entry.name))
                        entryCount++
                        require(entryCount <= MAX_ARCHIVE_ENTRIES) { "Texture archive contains too many entries" }
                        if (entry.isDirectory) continue
                        val cleanParts = cleanZipPath(entry.name) ?: error("Invalid texture archive path")
                        if (cleanParts.isEmpty() || !isTextureFile(cleanParts.last())) continue

                        val serial = normalizedTargetSerial ?: serialFromParts(cleanParts) ?: fallbackSerial ?: continue
                        val relativeParts = replacementRelativePath(cleanParts, serial)
                        if (relativeParts.isEmpty()) continue
                        val stagedRoot = File(canonicalStagingRoot, serial)
                        val stagedTarget = safeChild(stagedRoot, relativeParts)
                            ?: error("Invalid texture archive path")
                        val announcedSize = entry.size.coerceAtLeast(0L).coerceAtMost(MAX_TEXTURE_FILE_BYTES)
                        require(stagingRoot.usableSpace >= announcedSize + MIN_FREE_SPACE_BYTES) {
                            "Not enough free space to import texture archive"
                        }
                        stagedTarget.parentFile?.mkdirs()
                        stagedTarget.outputStream().use { output ->
                            val copied = zip.copyToWithLimit(output, MAX_TEXTURE_FILE_BYTES)
                            totalBytes += copied
                            require(totalBytes <= MAX_ARCHIVE_BYTES) { "Texture archive is too large" }
                        }
                        stagedFiles += stagedTarget.canonicalPath
                        importedNames += stagedTarget.canonicalPath.substringBeforeLast('.').lowercase(Locale.US)
                        importedSerials += serial
                    } finally {
                        zip.closeEntry()
                    }
                }
            }

            progress(TextureOptimizationProgress(0, 0, 0, 0, ""))
            pruneStaging(canonicalStagingRoot, stagedFiles)
            if (stagedFiles.isEmpty()) {
                TextureImportResult(success = false)
            } else {
                // Manual imports merge into a full staged snapshot so activation is atomic too.
                if (!replaceExisting) {
                    importedSerials.forEach { serial ->
                        val current = replacementsDir(serial)
                        if (current.isDirectory) {
                            current.walkTopDown().filter(File::isFile).forEach { source ->
                                val relative = source.relativeTo(current).invariantSeparatorsPath
                                val destination = safeChild(File(canonicalStagingRoot, serial), relative.split('/'))
                                    ?: error("Invalid existing texture path")
                                if (!destination.exists() && destination.canonicalPath.substringBeforeLast('.').lowercase(Locale.US) !in importedNames) {
                                    progress(TextureOptimizationProgress(0, 0, 0, 0, source.name))
                                    require(canonicalStagingRoot.usableSpace > source.length() + MIN_FREE_SPACE_BYTES) { "Not enough free space" }
                                    destination.parentFile?.mkdirs()
                                    source.copyTo(destination)
                                }
                            }
                        }
                    }
                }
                if (astcBlock >= 0) {
                    require(astcBlock in 0..13) { "Invalid ASTC quality" }
                    require(NativeApp.hasNativeCore && NativeApp.supportsAstcTextures()) {
                        "ASTC is not supported by this device"
                    }
                    optimization = optimizeStaged(canonicalStagingRoot, importedSerials, astcBlock, progress)
                }
                importedSerials.forEach { serial ->
                    progress(optimization)
                    replaceReplacementsAtomically(
                        stagedSerialRoot = File(canonicalStagingRoot, serial),
                        serial = serial
                    )
                }
                complete = true
                TextureImportResult(
                    success = true,
                    importedFiles = stagedFiles.size,
                    importedSerials = importedSerials,
                    optimizedFiles = optimization.optimized,
                    skippedFiles = optimization.skipped
                )
            }
        } catch (cancelled: kotlinx.coroutines.CancellationException) {
            throw cancelled
        } catch (error: Exception) {
            TextureImportResult(success = false, error = error.message.orEmpty())
        } finally {
            if (complete) stagingRoot.deleteRecursively()
        }
    }

    /** Keep reusable conversion checkpoints, but never resurrect files removed from a newer source. */
    private fun pruneStaging(root: File, expectedFiles: Set<String>) {
        val convertibleNames = expectedFiles.filter { File(it).extension.lowercase(Locale.US) in setOf("png", "dds") }
            .map { it.substringBeforeLast('.') }.toHashSet()
        root.walkTopDown().filter(File::isFile).forEach { file ->
            if (file.canonicalPath !in expectedFiles) {
                val baseName = when {
                    file.name.startsWith('.') && file.name.endsWith(".ktx2.astc-checkpoint") ->
                        file.name.removePrefix(".").removeSuffix(".ktx2.astc-checkpoint")
                    file.extension == "ktx2" -> file.nameWithoutExtension
                    else -> null
                }
                if (baseName == null || File(file.parentFile, baseName).canonicalPath !in convertibleNames) {
                    check(file.delete()) { "Could not clean texture staging" }
                }
            }
        }
    }

    private fun optimizeStaged(
        root: File,
        serials: Set<String>,
        block: Int,
        report: (TextureOptimizationProgress) -> Unit
    ): TextureOptimizationProgress {
        val mipPattern = Regex("-mip\\d+$", RegexOption.IGNORE_CASE)
        val inputs = serials.flatMap { serial ->
            File(root, serial).walkTopDown().filter(File::isFile).filter {
                it.extension.lowercase(Locale.US) in setOf("png", "dds") &&
                    !mipPattern.containsMatchIn(it.nameWithoutExtension)
            }.toList()
        }.sortedWith(compareBy<File> {
            // Visit completed checkpoints first on resume, then continue in a stable order.
            !File(it.parentFile, ".${it.nameWithoutExtension}.ktx2.astc-checkpoint").isFile
        }.thenBy { it.canonicalPath })
        var optimized = 0
        var skipped = 0
        inputs.forEachIndexed { index, source ->
            report(TextureOptimizationProgress(index, inputs.size, optimized, skipped, source.name))
            if (Thread.currentThread().isInterrupted) throw kotlinx.coroutines.CancellationException()
            val target = File(source.parentFile, source.nameWithoutExtension + ".ktx2")
            val temporary = File(source.parentFile, target.name + ".part")
            val checkpoint = File(source.parentFile, ".${target.name}.astc-checkpoint")
            val digest = java.security.MessageDigest.getInstance("SHA-256")
            fun hash(file: File) {
                file.inputStream().use { input ->
                    val buffer = ByteArray(64 * 1024)
                    while (true) { val count = input.read(buffer); if (count < 0) break; digest.update(buffer, 0, count) }
                }
            }
            digest.update("astcenc-5.7.0-fast-$block".toByteArray())
            hash(source)
            var side = 1
            while (true) {
                val mip = File(source.parentFile, "${source.nameWithoutExtension}-mip$side.${source.extension}")
                if (!mip.isFile) break
                hash(mip); side++
            }
            val fingerprint = digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
            val valid = target.isFile && checkpoint.isFile && checkpoint.readText() == fingerprint &&
                NativeApp.validateOptimizedTexture(target.absolutePath, block)
            val error = if (valid) null else {
                target.delete()
                temporary.delete()
                require(root.usableSpace > source.length() + MIN_FREE_SPACE_BYTES) { "Not enough free space" }
                NativeApp.convertTexture(source.absolutePath, temporary.absolutePath, block)
            }
            if (valid || (error == null && NativeApp.validateOptimizedTexture(temporary.absolutePath, block))) {
                if (!valid) {
                    require(temporary.renameTo(target)) { "Could not activate optimized texture" }
                    checkpoint.writeText(fingerprint)
                }
                source.delete()
                val mipCount = target.inputStream().use { input ->
                    val header = ByteArray(44)
                    check(input.read(header) == header.size)
                    java.nio.ByteBuffer.wrap(header).order(java.nio.ByteOrder.LITTLE_ENDIAN).getInt(40)
                }
                var mip = 1
                while (mip < mipCount) {
                    val sidecar = File(source.parentFile, "${source.nameWithoutExtension}-mip$mip.${source.extension}")
                    if (!sidecar.exists()) break
                    sidecar.delete()
                    mip++
                }
                optimized++
            } else {
                temporary.delete()
                skipped++ // Keep the original file; one unsupported texture must not break a pack.
            }
            report(TextureOptimizationProgress(index + 1, inputs.size, optimized, skipped, source.name))
        }
        return TextureOptimizationProgress(inputs.size, inputs.size, optimized, skipped, "")
    }

    fun optimizeInstalled(
        serial: String,
        block: Int,
        operationId: String,
        progress: (TextureOptimizationProgress) -> Unit
    ): TextureImportResult {
        val normalized = normalizeSerial(serial) ?: return TextureImportResult(false)
        require(operationId.matches(Regex("[A-Za-z0-9_-]{1,128}")))
        if (block !in 0..13) return TextureImportResult(false, error = "Choose an ASTC quality first")
        if (!NativeApp.hasNativeCore || !NativeApp.supportsAstcTextures())
            return TextureImportResult(false, error = "ASTC is not supported by this device")
        val original = replacementsDir(normalized)
        val staging = File(texturesRoot(), ".texture-import-$operationId")
        val stagedSerial = File(staging, normalized)
        return try {
            require(original.isDirectory) { "Texture pack was not found" }
            val sourceFiles = linkedSetOf<String>()
            original.walkTopDown().filter(File::isFile).forEach { source ->
                progress(TextureOptimizationProgress(0, 0, 0, 0, ""))
                val relative = source.relativeTo(original).invariantSeparatorsPath
                val destination = safeChild(stagedSerial, relative.split('/')) ?: error("Invalid texture path")
                destination.parentFile?.mkdirs()
                require(staging.usableSpace > source.length() + MIN_FREE_SPACE_BYTES) { "Not enough free space" }
                source.copyTo(destination, overwrite = true)
                sourceFiles += destination.canonicalPath
            }
            pruneStaging(staging, sourceFiles)
            val result = optimizeStaged(staging, setOf(normalized), block, progress)
            progress(result)
            replaceReplacementsAtomically(stagedSerial, normalized)
            staging.deleteRecursively()
            TextureImportResult(true, result.total, setOf(normalized), result.optimized, result.skipped)
        } catch (cancelled: kotlinx.coroutines.CancellationException) { throw cancelled }
        catch (error: Exception) { TextureImportResult(false, error = error.message.orEmpty()) }
    }

    private fun replaceReplacementsAtomically(stagedSerialRoot: File, serial: String) {
        val root = texturesRoot().canonicalFile
        val targetGame = File(root, serial).canonicalFile
        require(targetGame.parentFile == root) { "Invalid texture target" }
        stagedSerialRoot.walkTopDown().filter { it.isFile && (it.name.endsWith(".astc-checkpoint") || it.name.endsWith(".ktx2.part")) }
            .forEach { check(it.delete()) { "Could not finish texture staging" } }
        TexturePackTransactions.activate(targetGame, stagedSerialRoot)
    }

    fun deletePack(serial: String): Boolean {
        if (!TextureInstallCoordinator.mutex.tryLock()) return false
        try {
        val normalized = normalizeSerial(serial) ?: return false
        val directory = existingGameDir(normalized) ?: return false
        return !directory.exists() || directory.deleteRecursively()
        } finally { TextureInstallCoordinator.mutex.unlock() }
    }

    fun clearDumps(serial: String): Boolean {
        if (!TextureInstallCoordinator.mutex.tryLock()) return false
        try {
        val normalized = normalizeSerial(serial) ?: return false
        val game = existingGameDir(normalized) ?: return false
        if (!game.exists()) return true
        val dumps = File(game, "dumps")
        if (!dumps.exists()) return true
        return dumps.deleteRecursively() && dumps.mkdirs()
        } finally { TextureInstallCoordinator.mutex.unlock() }
    }

    fun discardOperation(operationId: String) {
        require(operationId.matches(Regex("[A-Za-z0-9_-]{1,128}")))
        val root = texturesRoot().canonicalFile
        val stage = File(root, ".texture-import-$operationId").canonicalFile
        require(stage.parentFile == root)
        stage.deleteRecursively()
    }

    private fun buildPackInfo(folder: File, libraryTitles: Map<String, String>): TexturePackInfo? {
        val serial = normalizeSerial(folder.name) ?: return null
        val replacementDir = File(folder, "replacements")
        val dumpDir = File(folder, "dumps")
        val replacementFiles = textureFiles(replacementDir)
        val dumpFiles = textureFiles(dumpDir)
        val allFiles = replacementFiles + dumpFiles
        return TexturePackInfo(
            serial = serial,
            gameTitle = libraryTitles[serial]
                ?: compatibilityRepository.findBySerial(serial)?.title
                    ?.trim()
                    ?.takeIf { it.isNotBlank() && !it.equals(serial, ignoreCase = true) },
            replacementCount = replacementFiles.size,
            dumpCount = dumpFiles.size,
            sizeBytes = allFiles.sumOf { it.length() },
            lastModifiedAt = allFiles.maxOfOrNull { it.lastModified() } ?: folder.lastModified(),
            canOptimize = replacementFiles.any { it.extension.lowercase(Locale.US) in setOf("png", "dds") }
        )
    }

    private suspend fun loadLibraryTitlesBySerial(): Map<String, String> {
        val paths = preferences.gamePaths.first()
        if (paths.isEmpty()) return emptyMap()
        return libraryCacheRepository
            .loadSnapshot(GameLibraryCacheRepository.libraryKey(paths))
            .games
            .mapNotNull { game ->
                val serial = game.serial?.let(::normalizeSerial) ?: return@mapNotNull null
                val title = game.title.trim().takeIf { it.isNotBlank() && !it.equals(serial, ignoreCase = true) }
                    ?: return@mapNotNull null
                serial to title
            }
            .toMap()
    }

    private fun texturesRoot(): File = rootOverride ?: EmulatorStorage.texturesDir(context, preferences.getEmulatorDataPathSync())

    private fun gameDir(serial: String): File {
        return File(texturesRoot(), serial).apply { mkdirs() }
    }

    private fun existingGameDir(serial: String): File? {
        val root = texturesRoot().canonicalFile
        val target = File(root, serial).canonicalFile
        return target.takeIf { it.parentFile == root }
    }

    private fun replacementsDir(serial: String): File {
        return File(gameDir(serial), "replacements").apply { mkdirs() }
    }

    private fun dumpsDir(serial: String): File {
        return File(gameDir(serial), "dumps").apply { mkdirs() }
    }

    private fun textureFiles(root: File): List<File> {
        if (!root.exists()) return emptyList()
        return root.walkTopDown()
            .filter { it.isFile && isTextureFile(it.name) }
            .toList()
    }

    private fun isTextureFile(name: String): Boolean {
        return name.substringAfterLast('.', "").lowercase(Locale.US) in textureExtensions
    }

    private fun cleanZipPath(path: String): List<String>? {
        if (path.startsWith('/') || path.startsWith('\\')) return null
        val rawParts = path.replace('\\', '/').split('/').map { it.trim() }
        if (rawParts.any { it == ".." || it.contains(':') || it.indexOf('\u0000') >= 0 }) return null
        return rawParts
            .filter { it.isNotEmpty() && it != "." }
    }

    private fun serialFromParts(parts: List<String>): String? {
        return parts.firstNotNullOfOrNull(::findSerial)
    }

    private fun findSerial(text: String?): String? {
        if (text.isNullOrBlank()) return null
        val match = serialPattern.find(text) ?: return null
        return normalizeSerial(match.value)
    }

    private fun normalizeSerial(raw: String): String? {
        val compact = raw.trim().uppercase(Locale.US).replace('_', '-').replace(' ', '-')
        val match = serialPattern.find(compact) ?: return null
        val value = match.value.replace('_', '-').replace(' ', '-')
        return if ('-' in value) value else "${value.take(4)}-${value.drop(4)}"
    }

    private fun replacementRelativePath(parts: List<String>, serial: String): List<String> {
        val normalizedParts = parts.map { part -> normalizeSerial(part) ?: part }
        val serialIndex = normalizedParts.indexOfFirst { it.equals(serial, ignoreCase = true) }
        val replacementIndex = normalizedParts.indexOfFirst { it.equals("replacements", ignoreCase = true) }
        val startIndex = maxOf(serialIndex, replacementIndex).let { if (it >= 0) it + 1 else 0 }
        val relative = parts.drop(startIndex)
        return if (
            startIndex == 0 &&
            relative.size > 1 &&
            githubCodeloadRootPattern.matches(relative.first())
        ) {
            relative.drop(1)
        } else {
            relative
        }
    }

    private fun safeChild(root: File, relativeParts: List<String>): File? {
        val rootCanonical = root.canonicalFile
        val target = relativeParts.fold(rootCanonical) { current, part -> File(current, part) }.canonicalFile
        return if (target.path.startsWith(rootCanonical.path + File.separator)) target else null
    }

    private fun displayName(uri: Uri): String? {
        return runCatching {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
                ?.use { cursor ->
                    if (cursor.moveToFirst()) {
                        cursor.getString(cursor.getColumnIndexOrThrow(OpenableColumns.DISPLAY_NAME))
                    } else {
                        null
                    }
                }
        }.getOrNull() ?: uri.lastPathSegment
    }

    private companion object {
        const val MAX_ARCHIVE_ENTRIES = 50_000
        const val MAX_TEXTURE_FILE_BYTES = 512L * 1024L * 1024L
        const val MAX_ARCHIVE_BYTES = 12L * 1024L * 1024L * 1024L
        const val MIN_FREE_SPACE_BYTES = 512L * 1024L * 1024L
        val githubCodeloadRootPattern = Regex(".+-[0-9a-fA-F]{40}")
    }
}

private fun InputStream.copyToWithLimit(output: java.io.OutputStream, limit: Long): Long {
    val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
    var copied = 0L
    while (true) {
        val read = read(buffer)
        if (read < 0) return copied
        copied += read
        require(copied <= limit) { "Texture file is too large" }
        output.write(buffer, 0, read)
    }
}
