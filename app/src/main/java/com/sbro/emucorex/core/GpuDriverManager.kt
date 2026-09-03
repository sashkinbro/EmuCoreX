package com.sbro.emucorex.core

import android.content.Context
import android.net.Uri
import android.os.Build
import android.provider.OpenableColumns
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.util.Locale
import java.util.zip.ZipInputStream

data class InstalledGpuDriver(
    val name: String,
    val mainLibrary: String,
    val mainLibraryPath: String,
    val isUsable: Boolean
)

class GpuDriverManager(private val context: Context) {

    fun listInstalledDrivers(): List<InstalledGpuDriver> {
        val root = driversRoot()
        if (!root.exists()) return emptyList()
        return root.listFiles()
            .orEmpty()
            .filter { it.isDirectory }
            .mapNotNull { driverDir ->
                val mainLibrary = readMainLibraryName(driverDir) ?: return@mapNotNull null
                val mainLibraryFile = File(driverDir, mainLibrary)
                if (mainLibraryFile.isFile) {
                    ensureDriverLibraryPermissions(mainLibraryFile)
                }
                InstalledGpuDriver(
                    name = driverDir.name,
                    mainLibrary = mainLibrary,
                    mainLibraryPath = mainLibraryFile.absolutePath,
                    isUsable = isValidDriverLibrary(mainLibraryFile)
                )
            }
            .sortedBy { it.name.lowercase() }
    }

    fun installFromArchive(uri: Uri): String {
        val archiveName = queryDisplayName(uri)
            ?: uri.lastPathSegment
            ?: "custom-driver.zip"
        val input = context.contentResolver.openInputStream(uri)
            ?: error("Could not open archive")
        return input.use { installFromArchive(it, archiveName) }
    }

    fun installFromArchive(file: File): String {
        file.inputStream().use { input ->
            return installFromArchive(input, file.name)
        }
    }

    fun remove(driverName: String) {
        File(driversRoot(), driverName).deleteRecursively()
    }

    fun readMainLibraryPath(driverName: String): String? {
        val driverDir = File(driversRoot(), driverName)
        val mainLibrary = readMainLibraryName(driverDir) ?: return null
        return File(driverDir, mainLibrary).absolutePath
    }

    fun resolveUsableDriverPath(preferredPath: String?): String? {
        if (!GpuDriverCompatibility.supportsAdrenoToolsCustomDrivers()) {
            return null
        }

        preferredPath
            ?.takeIf { it.isNotBlank() }
            ?.let(::File)
            ?.takeIf(::isValidDriverLibrary)
            ?.let { file ->
                ensureDriverLibraryPermissions(file)
                return file.absolutePath
            }

        return listInstalledDrivers()
            .firstOrNull { it.isUsable }
            ?.mainLibraryPath
    }

    private fun installFromArchive(input: InputStream, archiveName: String): String {
        val driverName = archiveName.substringBeforeLast('.').ifBlank { "custom-driver" }
        val targetDir = File(driversRoot(), driverName)

        if (targetDir.exists()) {
            targetDir.deleteRecursively()
        }
        targetDir.mkdirs()

        val extractedFiles = mutableListOf<String>()
        ZipInputStream(input).use { zip ->
            generateSequence { zip.nextEntry }.forEach { entry ->
                val entryName = entry.name ?: return@forEach
                if (entry.isDirectory) return@forEach
                val normalizedEntryName = entryName.replace('\\', '/').trimStart('/')
                if (normalizedEntryName.isBlank() || normalizedEntryName.contains("..")) {
                    error("Archive contains an invalid file path")
                }
                val outFile = File(targetDir, normalizedEntryName)
                val canonicalTarget = targetDir.canonicalFile
                val canonicalOutFile = outFile.canonicalFile
                if (!canonicalOutFile.toPath().startsWith(canonicalTarget.toPath())) {
                    error("Archive contains an invalid file path")
                }
                outFile.parentFile?.mkdirs()
                FileOutputStream(outFile).use { output ->
                    zip.copyTo(output)
                }
                if (outFile.extension.equals("so", ignoreCase = true)) {
                    ensureDriverLibraryPermissions(outFile)
                }
                extractedFiles += normalizedEntryName
            }
        }

        val selectedDriver = selectMainDriverFile(extractedFiles) ?: run {
            targetDir.deleteRecursively()
            error("Archive does not contain a Vulkan driver file")
        }

        // A corrupt or truncated .so would only explode later as a native
        // crash inside the third-party driver (e.g. Turnip SIGSEGV in
        // strncmp). Reject it here while we can still report a clean error.
        if (!isValidDriverLibrary(File(targetDir, selectedDriver))) {
            targetDir.deleteRecursively()
            error("Archive contains a corrupt Vulkan driver file")
        }

        File(targetDir, "driver_name.txt").writeText("$selectedDriver\n")
        return driverName
    }

    private fun driversRoot(): File = File(context.filesDir, "driver")

    private fun readMainLibraryName(driverDir: File): String? {
        val metadataFile = File(driverDir, "driver_name.txt")
        if (!metadataFile.isFile) return null
        return metadataFile.readText()
            .lineSequence()
            .map(String::trim)
            .firstOrNull { it.isNotEmpty() }
    }

    private fun ensureDriverLibraryPermissions(file: File) {
        file.setReadable(true, true)
        file.setWritable(true, true)
        file.setExecutable(true, true)
    }

    private fun selectMainDriverFile(extractedFiles: List<String>): String? {
        val sharedLibraries = extractedFiles
            .filter { it.endsWith(".so", ignoreCase = true) }
        if (sharedLibraries.isEmpty()) return null

        return sharedLibraries.firstOrNull { file ->
            val name = file.substringAfterLast('/')
            name.equals("libvulkan.so", ignoreCase = true)
        } ?: sharedLibraries.firstOrNull { file ->
            val name = file.substringAfterLast('/')
            name.startsWith("vulkan.", ignoreCase = true) || name.startsWith("libvulkan.", ignoreCase = true)
        } ?: sharedLibraries.firstOrNull { file ->
            file.contains("vulkan", ignoreCase = true)
        }
    }

    private fun queryDisplayName(uri: Uri): String? {
        val projection = arrayOf(OpenableColumns.DISPLAY_NAME)
        return context.contentResolver.query(uri, projection, null, null, null)?.use { cursor ->
            val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (index >= 0 && cursor.moveToFirst()) cursor.getString(index) else null
        }
    }
}

internal fun isValidDriverLibrary(file: File): Boolean {
    if (!file.isFile || file.length() <= 0L) return false
    return try {
        file.inputStream().use { input ->
            val magic = ByteArray(4)
            if (input.read(magic) != magic.size) return false
            // ELF magic: 0x7F 'E' 'L' 'F'. Catches empty files, HTML error
            // pages and misnamed zips before they reach dlopen.
            magic[0] == 0x7F.toByte() && magic[1] == 'E'.code.toByte() &&
                magic[2] == 'L'.code.toByte() && magic[3] == 'F'.code.toByte()
        }
    } catch (_: Exception) {
        false
    }
}

object GpuDriverCompatibility {
    fun supportsAdrenoToolsCustomDrivers(): Boolean {
        val deviceInfo = buildList {
            add(Build.BOARD)
            add(Build.BRAND)
            add(Build.DEVICE)
            add(Build.HARDWARE)
            add(Build.MANUFACTURER)
            add(Build.MODEL)
            add(Build.PRODUCT)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                add(Build.SOC_MANUFACTURER)
                add(Build.SOC_MODEL)
            }
        }
            .joinToString(" ")
            .lowercase(Locale.US)

        val qualcommSignals = listOf(
            "adreno",
            "qcom",
            "qualcomm",
            "qti",
            "snapdragon",
            "msm",
            "sdm",
            "sm8",
            "sm7",
            "sm6",
            "kalama",
            "lahaina",
            "taro",
            "waipio"
        )
        if (qualcommSignals.any { it in deviceInfo }) {
            return true
        }

        val knownNonAdrenoSignals = listOf(
            "mediatek",
            "mtk",
            "dimensity",
            "helio",
            "exynos",
            "mali",
            "kirin",
            "hisilicon",
            "tensor",
            "unisoc",
            "spreadtrum",
            "powervr",
            "imgtec"
        )
        return knownNonAdrenoSignals.none { it in deviceInfo } && Regex("""\bsm[0-9]{3,4}\b""").containsMatchIn(deviceInfo)
    }
}
