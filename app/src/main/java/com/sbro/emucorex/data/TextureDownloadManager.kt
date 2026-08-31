package com.sbro.emucorex.data

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.SystemClock
import androidx.core.app.NotificationCompat
import androidx.work.BackoffPolicy
import androidx.work.Constraints
import androidx.work.CoroutineWorker
import androidx.work.Data
import androidx.work.ExistingWorkPolicy
import androidx.work.ForegroundInfo
import androidx.work.NetworkType
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.OutOfQuotaPolicy
import androidx.work.WorkManager
import androidx.work.WorkerParameters
import com.sbro.emucorex.R
import com.sbro.emucorex.core.BackupSessionGate
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.delay
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import java.util.Locale
import java.util.concurrent.TimeUnit
import kotlin.coroutines.cancellation.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

enum class TextureDownloadStatus {
    QUEUED,
    DOWNLOADING,
    PAUSED,
    WAITING_NETWORK,
    VERIFYING,
    INSTALLING,
    OPTIMIZING,
    COMPLETED,
    FAILED,
    CANCELLED;

    val isActive: Boolean
        get() = this == QUEUED || this == DOWNLOADING || this == WAITING_NETWORK ||
            this == VERIFYING || this == INSTALLING || this == OPTIMIZING
}

data class TextureDownloadTask(
    val key: String,
    val packId: String,
    val packName: String,
    val version: String,
    val serial: String,
    val downloadUrl: String,
    val sha256: String,
    val totalBytes: Long,
    val downloadedBytes: Long,
    val bytesPerSecond: Long,
    val etaSeconds: Long,
    val status: TextureDownloadStatus,
    val error: String,
    val updatedAt: Long,
    val astcBlock: Int = -1,
    val textureRoot: String = "",
    val optimizedCount: Int = 0,
    val optimizationTotal: Int = 0,
    val optimizationElapsedMs: Long = 0,
    val skippedCount: Int = 0,
    val parts: List<RemoteTexturePart> = emptyList()
) {
    val progress: Float
        get() = if (status == TextureDownloadStatus.OPTIMIZING) {
            if (optimizationTotal > 0) optimizedCount.toFloat() / optimizationTotal else 0f
        } else if (totalBytes <= 0L) 0f
        else (downloadedBytes.toDouble() / totalBytes.toDouble()).toFloat().coerceIn(0f, 1f)
}

class TextureDownloadManager(context: Context) {
    private val appContext = context.applicationContext
    private val workManager = WorkManager.getInstance(appContext)

    fun tasks(): List<TextureDownloadTask> = TextureDownloadStore.readAll(appContext)

    fun enqueue(pack: RemoteTexturePack, serial: String, quality: Int = TextureOptimizationManager(appContext).block): Boolean {
        val manual = TextureOptimizationManager(appContext).state()
        if (manual.active || manual.phase in setOf("paused", "failed")) return false
        if (tasks().any { it.serial.equals(serial, true) && (it.status.isActive || it.status == TextureDownloadStatus.PAUSED) }) return false
        require(pack.serials.any { it.equals(serial, ignoreCase = true) }) {
            "Texture pack is not compatible with the selected game serial"
        }
        val task = TextureDownloadStore.createOrReset(appContext, pack, serial, quality)
        enqueue(task)
        return true
    }

    fun pause(key: String) {
        TextureDownloadStore.update(appContext, key) { task ->
            task.copy(
                status = TextureDownloadStatus.PAUSED,
                bytesPerSecond = 0L,
                etaSeconds = 0L,
                error = "",
                updatedAt = System.currentTimeMillis()
            )
        }
        workManager.cancelUniqueWork(workName(key))
    }

    fun resume(key: String) {
        val task = TextureDownloadStore.update(appContext, key) { current ->
            current.copy(
                status = TextureDownloadStatus.QUEUED,
                downloadedBytes = TextureDownloadStore.downloadedBytes(appContext, current),
                bytesPerSecond = 0L,
                etaSeconds = 0L,
                error = "",
                updatedAt = System.currentTimeMillis()
            )
        } ?: return
        enqueue(task)
    }

    fun cancel(key: String) {
        TextureDownloadStore.update(appContext, key) { task ->
            task.copy(
                status = TextureDownloadStatus.CANCELLED,
                bytesPerSecond = 0L,
                etaSeconds = 0L,
                error = "",
                updatedAt = System.currentTimeMillis()
            )
        }
        workManager.cancelUniqueWork(workName(key))
        TextureDownloadStore.discardPayload(appContext, key)
        cleanupStage(key)
    }

    fun remove(key: String) {
        cancel(key)
        TextureDownloadStore.remove(appContext, key)
    }

    private fun cleanupStage(key: String) {
        val task = TextureDownloadStore.read(appContext, key)
        workManager.enqueue(OneTimeWorkRequestBuilder<TextureOptimizationCleanupWorker>()
            .setInputData(Data.Builder().putString("id", key).putBoolean("download", true)
                .putString("root", task?.textureRoot?.takeIf(String::isNotBlank)).build()).build())
    }

    private fun enqueue(task: TextureDownloadTask) {
        val request = OneTimeWorkRequestBuilder<TextureDownloadWorker>()
            .setInputData(Data.Builder().putString(TextureDownloadWorker.KEY_TASK, task.key).build())
            .setConstraints(Constraints.Builder().setRequiredNetworkType(NetworkType.CONNECTED).build())
            .setBackoffCriteria(BackoffPolicy.EXPONENTIAL, 10, TimeUnit.SECONDS)
            .setExpedited(OutOfQuotaPolicy.RUN_AS_NON_EXPEDITED_WORK_REQUEST)
            .addTag(WORK_TAG)
            .addTag("$WORK_TAG:${task.key}")
            .build()
        workManager.enqueueUniqueWork(workName(task.key), ExistingWorkPolicy.REPLACE, request)
    }

    private companion object {
        const val WORK_TAG = "texture_pack_download"
        fun workName(key: String): String = "texture_pack_download_$key"
    }
}

class TextureDownloadWorker(
    appContext: Context,
    workerParams: WorkerParameters
) : CoroutineWorker(appContext, workerParams) {
    override suspend fun doWork(): Result = withContext(Dispatchers.IO) {
        val key = inputData.getString(KEY_TASK).orEmpty()
        var task = TextureDownloadStore.read(applicationContext, key) ?: return@withContext Result.failure()
        if (task.status == TextureDownloadStatus.PAUSED || task.status == TextureDownloadStatus.CANCELLED) {
            return@withContext Result.success()
        }

        try {
            task = updateTask(task) {
                it.copy(
                    status = TextureDownloadStatus.DOWNLOADING,
                    bytesPerSecond = 0L,
                    etaSeconds = 0L,
                    error = ""
                )
            }
            setForeground(foregroundInfo(task))
            val parts = download(task)

            task = updateTask(task) {
                it.copy(
                    status = TextureDownloadStatus.VERIFYING,
                    downloadedBytes = it.totalBytes,
                    bytesPerSecond = 0L,
                    etaSeconds = 0L
                )
            }
            setForeground(foregroundInfo(task))
            val archive = assembleAndVerify(parts, task)

            task = updateTask(task) {
                it.copy(status = TextureDownloadStatus.INSTALLING, bytesPerSecond = 0L, etaSeconds = 0L)
            }
            setForeground(foregroundInfo(task))
            val preferences = AppPreferences(applicationContext)
            val coroutine = currentCoroutineContext()
            val previousCompleted = task.optimizedCount
            val previousTotal = task.optimizationTotal
            val previousElapsed = task.optimizationElapsedMs
            val optimizationStart = SystemClock.elapsedRealtime()
            var lastOptimizationReport = 0L
            val installed = TextureInstallCoordinator.mutex.withLock {
                BackupSessionGate.awaitStopped {
                    coroutine.ensureActive()
                    TexturePackRepository(applicationContext, preferences, task.textureRoot.takeIf(String::isNotBlank)?.let(::File))
                        .installRemotePack(archive, task.serial, task.astcBlock, task.key) { progress ->
                            coroutine.ensureActive()
                            if (BackupSessionGate.gameBusy) throw TextureGameRequested()
                            val now = SystemClock.elapsedRealtime()
                            val elapsed = previousElapsed + now - optimizationStart
                            val completed = if (progress.total == previousTotal || progress.total == 0) maxOf(previousCompleted, progress.completed) else progress.completed
                            val total = if (progress.total == 0) previousTotal else progress.total
                            if (now - lastOptimizationReport < 350 && (progress.total == 0 || progress.completed != progress.total))
                                return@installRemotePack
                            lastOptimizationReport = now
                            task = updateTask(task) { current ->
                                current.copy(status = if (progress.total > 0) TextureDownloadStatus.OPTIMIZING else TextureDownloadStatus.INSTALLING,
                                    optimizedCount = completed, optimizationTotal = total, skippedCount = progress.skipped, optimizationElapsedMs = elapsed,
                                    etaSeconds = if (completed > 0) elapsed * (total - completed) / completed / 1000 else 0)
                            }
                            applicationContext.getSystemService(NotificationManager::class.java)
                                .notify(notificationId(task.key), foregroundInfo(task).notification)
                        }
                }
            }
            if (!installed.success) throw PermanentDownloadException(installed.error.ifBlank { "Texture archive could not be installed" })

            val installState = RemoteContentInstallState(applicationContext)
            installState.removeTexturesForSerial(task.serial)
            installState.recordTexture(task.packId, task.version, task.serial)
            preferences.setTextureReplacementsEnabled(true)
            TextureDownloadStore.discardPayload(applicationContext, key)
            updateTask(task) {
                it.copy(
                    status = TextureDownloadStatus.COMPLETED,
                    downloadedBytes = it.totalBytes,
                    bytesPerSecond = 0L,
                    etaSeconds = 0L,
                    error = ""
                )
            }
            Result.success()
        } catch (waiting: TextureGameRequested) {
            Result.retry()
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (permanent: PermanentDownloadException) {
            TextureDownloadStore.discardPayload(applicationContext, key)
            markFailed(key, permanent.message.orEmpty())
            Result.failure()
        } catch (network: IOException) {
            val current = TextureDownloadStore.read(applicationContext, key)
            if (current?.status != TextureDownloadStatus.PAUSED &&
                current?.status != TextureDownloadStatus.CANCELLED
            ) {
                TextureDownloadStore.update(applicationContext, key) {
                    it.copy(
                        status = TextureDownloadStatus.WAITING_NETWORK,
                        downloadedBytes = TextureDownloadStore.downloadedBytes(applicationContext, it),
                        bytesPerSecond = 0L,
                        etaSeconds = 0L,
                        error = "",
                        updatedAt = System.currentTimeMillis()
                    )
                }
            }
            Result.retry()
        } catch (error: Throwable) {
            markFailed(key, error.message.orEmpty())
            Result.failure()
        }
    }

    private suspend fun download(initialTask: TextureDownloadTask): List<File> {
        val payloads = initialTask.parts.ifEmpty {
            listOf(RemoteTexturePart(initialTask.downloadUrl, initialTask.totalBytes, initialTask.sha256))
        }
        val files = mutableListOf<File>()
        var completedBytes = 0L
        payloads.forEachIndexed { index, payload ->
            val part = TextureDownloadStore.partialFile(
                applicationContext,
                initialTask.key,
                index,
                payloads.size
            )
            files += downloadPart(initialTask, payload, part, completedBytes)
            completedBytes += payload.sizeBytes
        }
        return files
    }

    private suspend fun downloadPart(
        initialTask: TextureDownloadTask,
        payload: RemoteTexturePart,
        part: File,
        completedBytes: Long
    ): File {
        part.parentFile?.mkdirs()
        if (part.length() > payload.sizeBytes) part.delete()
        var offset = part.length()
        if (offset == payload.sizeBytes && offset > 0L) return part
        val connection = openDownloadConnection(payload.downloadUrl, offset)
        try {
            val response = connection.responseCode
            val append = offset > 0L && response == HttpURLConnection.HTTP_PARTIAL
            if (append && contentRangeStart(connection) != offset) {
                part.delete()
                throw IOException("Download server returned an invalid resume range")
            }
            if (offset > 0L && !append) {
                part.delete()
                offset = 0L
            }
            val responseBytes = connection.contentLengthLong
            if (responseBytes > 0L && offset + responseBytes > payload.sizeBytes) {
                throw PermanentDownloadException("Download is larger than the catalog entry")
            }

            var partDownloaded = offset
            var downloaded = completedBytes + partDownloaded
            var smoothedSpeed = 0.0
            var lastSampleAt = SystemClock.elapsedRealtime()
            var lastSampleBytes = downloaded
            var task = updateTask(initialTask) {
                it.copy(
                    status = TextureDownloadStatus.DOWNLOADING,
                    downloadedBytes = downloaded,
                    bytesPerSecond = 0L,
                    etaSeconds = 0L,
                    error = ""
                )
            }
            RandomAccessFile(part, "rw").use { output ->
                output.seek(offset)
                connection.inputStream.buffered(DOWNLOAD_BUFFER_BYTES).use { input ->
                    val buffer = ByteArray(DOWNLOAD_BUFFER_BYTES)
                    while (true) {
                        currentCoroutineContext().ensureActive()
                        if (isStopped) throw CancellationException("Texture download stopped")
                        val persisted = TextureDownloadStore.read(applicationContext, task.key)
                        if (persisted?.status == TextureDownloadStatus.PAUSED ||
                            persisted?.status == TextureDownloadStatus.CANCELLED
                        ) {
                            throw CancellationException("Texture download paused")
                        }
                        val read = input.read(buffer)
                        if (read < 0) break
                        output.write(buffer, 0, read)
                        partDownloaded += read
                        downloaded = completedBytes + partDownloaded
                        if (partDownloaded > payload.sizeBytes) {
                            throw PermanentDownloadException("Download is larger than the catalog entry")
                        }

                        val now = SystemClock.elapsedRealtime()
                        val elapsed = now - lastSampleAt
                        if (elapsed >= PROGRESS_UPDATE_MS) {
                            val instant = (downloaded - lastSampleBytes) * 1000.0 / elapsed.coerceAtLeast(1L)
                            smoothedSpeed = if (smoothedSpeed <= 0.0) instant else smoothedSpeed * 0.72 + instant * 0.28
                            val speed = smoothedSpeed.toLong().coerceAtLeast(0L)
                            val remaining = (task.totalBytes - downloaded).coerceAtLeast(0L)
                            val eta = if (speed > 0L) remaining / speed else 0L
                            task = updateTask(task) {
                                it.copy(
                                    status = TextureDownloadStatus.DOWNLOADING,
                                    downloadedBytes = downloaded,
                                    bytesPerSecond = speed,
                                    etaSeconds = eta,
                                    error = ""
                                )
                            }
                            setProgress(
                                Data.Builder()
                                    .putLong(PROGRESS_DOWNLOADED, downloaded)
                                    .putLong(PROGRESS_TOTAL, task.totalBytes)
                                    .putLong(PROGRESS_SPEED, speed)
                                    .putLong(PROGRESS_ETA, eta)
                                    .build()
                            )
                            setForeground(foregroundInfo(task))
                            lastSampleAt = now
                            lastSampleBytes = downloaded
                        }
                    }
                }
            }
            if (partDownloaded != payload.sizeBytes) throw IOException("Download ended before the expected size")
            return part
        } finally {
            connection.disconnect()
        }
    }

    private fun contentRangeStart(connection: HttpURLConnection): Long? {
        val header = connection.getHeaderField("Content-Range") ?: return null
        return CONTENT_RANGE.matchEntire(header.trim())?.groupValues?.get(1)?.toLongOrNull()
    }

    private fun openDownloadConnection(rawUrl: String, offset: Long): HttpURLConnection {
        var next = URL(rawUrl)
        require(next.protocol.equals("https", ignoreCase = true))
        repeat(MAX_REDIRECTS + 1) { redirectCount ->
            val connection = (next.openConnection() as HttpURLConnection).apply {
                connectTimeout = CONNECT_TIMEOUT_MS
                readTimeout = READ_TIMEOUT_MS
                instanceFollowRedirects = false
                setRequestProperty("Accept", "application/octet-stream, */*")
                setRequestProperty("Accept-Encoding", "identity")
                setRequestProperty("User-Agent", "EmuCoreX-Android")
                if (offset > 0L) setRequestProperty("Range", "bytes=$offset-")
            }
            try {
                connection.connect()
            } catch (error: Throwable) {
                connection.disconnect()
                throw error
            }
            val code = connection.responseCode
            if (code in REDIRECT_CODES) {
                val location = connection.getHeaderField("Location")
                if (location == null) {
                    connection.disconnect()
                    throw IOException("Redirect has no destination")
                }
                connection.disconnect()
                if (redirectCount >= MAX_REDIRECTS) throw IOException("Too many redirects")
                next = URL(next, location)
                if (!next.protocol.equals("https", ignoreCase = true)) {
                    throw PermanentDownloadException("Insecure download redirect")
                }
            } else {
                when {
                    code in 200..299 -> return connection
                    code == 408 || code == 429 || code >= 500 -> {
                        connection.disconnect()
                        throw IOException("HTTP $code")
                    }
                    else -> {
                        connection.disconnect()
                        throw PermanentDownloadException("HTTP $code")
                    }
                }
            }
        }
        throw IOException("Could not open download")
    }

    private fun assembleAndVerify(files: List<File>, task: TextureDownloadTask): File {
        val payloads = task.parts.ifEmpty {
            listOf(RemoteTexturePart(task.downloadUrl, task.totalBytes, task.sha256))
        }
        if (files.size != payloads.size) throw PermanentDownloadException("Texture archive parts are incomplete")
        val archive = TextureDownloadStore.archiveFile(applicationContext, task.key)
        archive.delete()
        val wholeDigest = MessageDigest.getInstance("SHA-256")
        var assembledBytes = 0L
        try {
            archive.outputStream().buffered(VERIFY_BUFFER_BYTES).use { output ->
                val buffer = ByteArray(VERIFY_BUFFER_BYTES)
                files.zip(payloads).forEach { (file, payload) ->
                    if (file.length() != payload.sizeBytes) {
                        throw PermanentDownloadException("Downloaded part size does not match the catalog")
                    }
                    val partDigest = MessageDigest.getInstance("SHA-256")
                    file.inputStream().buffered(VERIFY_BUFFER_BYTES).use { input ->
                        while (true) {
                            val read = input.read(buffer)
                            if (read < 0) break
                            partDigest.update(buffer, 0, read)
                            wholeDigest.update(buffer, 0, read)
                            output.write(buffer, 0, read)
                            assembledBytes += read
                        }
                    }
                    val partHash = partDigest.digest().toHex()
                    if (!partHash.equals(payload.sha256, ignoreCase = true)) {
                        throw PermanentDownloadException("Downloaded part SHA-256 does not match the catalog")
                    }
                }
            }
            if (assembledBytes != task.totalBytes) {
                throw PermanentDownloadException("Downloaded size does not match the catalog")
            }
            if (!wholeDigest.digest().toHex().equals(task.sha256, ignoreCase = true)) {
                throw PermanentDownloadException("Downloaded SHA-256 does not match the catalog")
            }
            return archive
        } catch (error: Throwable) {
            archive.delete()
            throw error
        }
    }

    private fun updateTask(
        fallback: TextureDownloadTask,
        transform: (TextureDownloadTask) -> TextureDownloadTask
    ): TextureDownloadTask = TextureDownloadStore.update(applicationContext, fallback.key) { current ->
        if (current.status == TextureDownloadStatus.PAUSED || current.status == TextureDownloadStatus.CANCELLED)
            throw CancellationException()
        transform(current).copy(updatedAt = System.currentTimeMillis())
    } ?: fallback

    private fun markFailed(key: String, message: String) {
        val current = TextureDownloadStore.read(applicationContext, key) ?: return
        if (current.status == TextureDownloadStatus.PAUSED || current.status == TextureDownloadStatus.CANCELLED) return
        TextureDownloadStore.update(applicationContext, key) {
            it.copy(
                status = TextureDownloadStatus.FAILED,
                downloadedBytes = TextureDownloadStore.downloadedBytes(applicationContext, current),
                bytesPerSecond = 0L,
                etaSeconds = 0L,
                error = message.take(240),
                updatedAt = System.currentTimeMillis()
            )
        }
    }

    private fun foregroundInfo(task: TextureDownloadTask): ForegroundInfo {
        val manager = applicationContext.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        manager.createNotificationChannel(
            NotificationChannel(
                NOTIFICATION_CHANNEL,
                applicationContext.getString(R.string.texture_download_channel_name),
                NotificationManager.IMPORTANCE_LOW
            ).apply { description = applicationContext.getString(R.string.texture_download_channel_description) }
        )
        val percent = (task.progress * 100).toInt().coerceIn(0, 100)
        val contentIntent = applicationContext.packageManager
            .getLaunchIntentForPackage(applicationContext.packageName)
            ?.let { intent ->
                PendingIntent.getActivity(
                    applicationContext,
                    notificationId(task.key),
                    intent,
                    PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
                )
            }
        val pauseIntent = TextureDownloadActionReceiver.pendingIntent(
            applicationContext,
            task.key,
            TextureDownloadActionReceiver.ACTION_PAUSE
        )
        val cancelIntent = TextureDownloadActionReceiver.pendingIntent(
            applicationContext,
            task.key,
            TextureDownloadActionReceiver.ACTION_CANCEL
        )
        val notification = NotificationCompat.Builder(applicationContext, NOTIFICATION_CHANNEL)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(task.packName)
            .setContentText(notificationText(task))
            .setContentIntent(contentIntent)
            .setOnlyAlertOnce(true)
            .setOngoing(true)
            .setProgress(100, percent, task.totalBytes <= 0L)
            .addAction(0, applicationContext.getString(R.string.emulation_pause), pauseIntent)
            .addAction(0, applicationContext.getString(R.string.cancel), cancelIntent)
            .build()
        return ForegroundInfo(
            notificationId(task.key),
            notification,
            ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
        )
    }

    private fun notificationText(task: TextureDownloadTask): String = when (task.status) {
        TextureDownloadStatus.VERIFYING -> applicationContext.getString(R.string.texture_download_status_verifying)
        TextureDownloadStatus.OPTIMIZING -> applicationContext.getString(R.string.texture_opt_progress, task.optimizedCount, task.optimizationTotal, (task.progress * 100).toInt())
        TextureDownloadStatus.INSTALLING -> applicationContext.getString(R.string.texture_download_status_installing)
        else -> {
            val speed = formatDownloadBytes(task.bytesPerSecond)
            val eta = if (task.etaSeconds > 0L) formatDownloadDuration(task.etaSeconds)
            else applicationContext.getString(R.string.texture_download_eta_estimating)
            applicationContext.getString(
                R.string.texture_download_notification_progress,
                (task.progress * 100).toInt(),
                speed,
                eta
            )
        }
    }

    companion object {
        const val KEY_TASK = "texture_download_task"
        const val PROGRESS_DOWNLOADED = "downloaded"
        const val PROGRESS_TOTAL = "total"
        const val PROGRESS_SPEED = "speed"
        const val PROGRESS_ETA = "eta"
        private const val NOTIFICATION_CHANNEL = "texture_downloads"
        private const val CONNECT_TIMEOUT_MS = 20_000
        private const val READ_TIMEOUT_MS = 45_000
        private const val DOWNLOAD_BUFFER_BYTES = 256 * 1024
        private const val VERIFY_BUFFER_BYTES = 1024 * 1024
        private const val PROGRESS_UPDATE_MS = 750L
        private const val MAX_REDIRECTS = 8
        private val REDIRECT_CODES = setOf(301, 302, 303, 307, 308)
        private val CONTENT_RANGE = Regex("bytes\\s+(\\d+)-\\d+/(?:\\d+|\\*)", RegexOption.IGNORE_CASE)

        private fun notificationId(key: String): Int = 20_000 + (key.hashCode() and 0x3FFF)
    }
}

class TextureDownloadActionReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val key = intent.getStringExtra(EXTRA_KEY).orEmpty()
        if (!TextureDownloadStore.isValidKey(key)) return
        val manager = TextureDownloadManager(context)
        when (intent.action) {
            ACTION_PAUSE -> manager.pause(key)
            ACTION_CANCEL -> manager.cancel(key)
        }
    }

    companion object {
        const val ACTION_PAUSE = "com.sbro.emucorex.action.PAUSE_TEXTURE_DOWNLOAD"
        const val ACTION_CANCEL = "com.sbro.emucorex.action.CANCEL_TEXTURE_DOWNLOAD"
        private const val EXTRA_KEY = "task_key"

        fun pendingIntent(context: Context, key: String, action: String): PendingIntent =
            PendingIntent.getBroadcast(
                context,
                (key + action).hashCode(),
                Intent(context, TextureDownloadActionReceiver::class.java)
                    .setAction(action)
                    .putExtra(EXTRA_KEY, key),
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            )
    }
}

internal object TextureDownloadStore {
    private const val ROOT = "texture-downloads"
    private const val MANIFEST = "task.json"
    private const val PARTIAL = "download.part"
    private const val ARCHIVE = "download.zip"
    private val lock = Any()

    fun createOrReset(context: Context, pack: RemoteTexturePack, serial: String, quality: Int): TextureDownloadTask = synchronized(lock) {
        val key = key(pack.id, serial)
        val directory = directory(context, key).apply { mkdirs() }
        val previous = readUnlocked(context, key)
        val samePayload = previous?.downloadUrl == pack.downloadUrl &&
            previous.sha256.equals(pack.sha256, ignoreCase = true) &&
            previous.totalBytes == pack.sizeBytes &&
            previous.parts == pack.parts
        if (!samePayload) {
            directory.listFiles()
                ?.filter { it.name == PARTIAL || it.name.startsWith("$PARTIAL.") || it.name == ARCHIVE }
                ?.forEach(File::delete)
        }
        val downloaded = previous
            ?.takeIf { samePayload }
            ?.let { downloadedBytes(context, it) }
            ?.coerceAtMost(pack.sizeBytes)
            ?: 0L
        TextureDownloadTask(
            key = key,
            packId = pack.id,
            packName = pack.name,
            version = pack.version,
            serial = serial.uppercase(Locale.US),
            downloadUrl = pack.downloadUrl,
            astcBlock = quality,
            textureRoot = com.sbro.emucorex.core.EmulatorStorage.texturesDir(context, AppPreferences(context).getEmulatorDataPathSync()).absolutePath,
            sha256 = pack.sha256.uppercase(Locale.US),
            totalBytes = pack.sizeBytes,
            downloadedBytes = downloaded,
            bytesPerSecond = 0L,
            etaSeconds = 0L,
            status = TextureDownloadStatus.QUEUED,
            error = "",
            updatedAt = System.currentTimeMillis(),
            parts = pack.parts
        ).also { writeUnlocked(context, it) }
    }

    fun read(context: Context, key: String): TextureDownloadTask? = synchronized(lock) {
        readUnlocked(context, key)
    }

    fun readAll(context: Context): List<TextureDownloadTask> = synchronized(lock) {
        root(context).listFiles()
            ?.asSequence()
            ?.filter(File::isDirectory)
            ?.mapNotNull { directory -> readUnlocked(context, directory.name) }
            ?.sortedByDescending(TextureDownloadTask::updatedAt)
            ?.toList()
            .orEmpty()
    }

    fun update(
        context: Context,
        key: String,
        transform: (TextureDownloadTask) -> TextureDownloadTask
    ): TextureDownloadTask? = synchronized(lock) {
        val current = readUnlocked(context, key) ?: return@synchronized null
        transform(current).also { writeUnlocked(context, it) }
    }

    fun partialFile(
        context: Context,
        key: String,
        index: Int = 0,
        partCount: Int = 1
    ): File = File(directory(context, key), if (partCount == 1) PARTIAL else "$PARTIAL.${index + 1}")
    fun archiveFile(context: Context, key: String): File = File(directory(context, key), ARCHIVE)

    fun downloadedBytes(context: Context, task: TextureDownloadTask): Long {
        val partCount = task.parts.size.coerceAtLeast(1)
        return (0 until partCount).sumOf { index ->
            partialFile(context, task.key, index, partCount).length()
        }.coerceAtMost(task.totalBytes)
    }

    fun discardPayload(context: Context, key: String) = synchronized(lock) {
        directory(context, key).listFiles()
            ?.filter { it.name == PARTIAL || it.name.startsWith("$PARTIAL.") }
            ?.forEach(File::delete)
        archiveFile(context, key).delete()
    }

    fun remove(context: Context, key: String) = synchronized(lock) {
        val target = directory(context, key).canonicalFile
        val root = root(context).canonicalFile
        if (target.parentFile == root) target.deleteRecursively()
    }

    fun isValidKey(key: String): Boolean = key.matches(Regex("[0-9a-f]{32}"))

    private fun readUnlocked(context: Context, key: String): TextureDownloadTask? {
        if (!isValidKey(key)) return null
        val file = File(directory(context, key), MANIFEST)
        if (!file.isFile) return null
        return runCatching {
            val json = JSONObject(file.readText())
            TextureDownloadTask(
                key = key,
                packId = json.getString("packId"),
                packName = json.getString("packName"),
                version = json.getString("version"),
                serial = json.getString("serial"),
                downloadUrl = json.getString("downloadUrl"),
                sha256 = json.getString("sha256"),
                totalBytes = json.getLong("totalBytes"),
                downloadedBytes = json.optLong("downloadedBytes", 0L),
                bytesPerSecond = json.optLong("bytesPerSecond", 0L),
                etaSeconds = json.optLong("etaSeconds", 0L),
                status = TextureDownloadStatus.valueOf(json.getString("status")),
                error = json.optString("error"),
                updatedAt = json.optLong("updatedAt", 0L),
                astcBlock = json.optInt("astcBlock", -1),
                textureRoot = json.optString("textureRoot", ""),
                optimizedCount = json.optInt("optimizedCount", 0),
                optimizationTotal = json.optInt("optimizationTotal", 0),
                optimizationElapsedMs = json.optLong("optimizationElapsedMs", 0),
                skippedCount = json.optInt("skippedCount", 0),
                parts = json.optJSONArray("parts")?.let { array ->
                    (0 until array.length()).map { index ->
                        val part = array.getJSONObject(index)
                        RemoteTexturePart(
                            downloadUrl = part.getString("downloadUrl"),
                            sizeBytes = part.getLong("sizeBytes"),
                            sha256 = part.getString("sha256")
                        )
                    }
                }.orEmpty()
            )
        }.getOrNull()
    }

    private fun writeUnlocked(context: Context, task: TextureDownloadTask) {
        val target = File(directory(context, task.key).apply { mkdirs() }, MANIFEST)
        val temporary = File(target.parentFile, "$MANIFEST.tmp")
        val json = JSONObject()
            .put("packId", task.packId)
        .put("astcBlock", task.astcBlock)
        .put("textureRoot", task.textureRoot)
        .put("optimizedCount", task.optimizedCount)
        .put("optimizationTotal", task.optimizationTotal)
        .put("optimizationElapsedMs", task.optimizationElapsedMs)
        .put("skippedCount", task.skippedCount)
            .put("packName", task.packName)
            .put("version", task.version)
            .put("serial", task.serial)
            .put("downloadUrl", task.downloadUrl)
            .put("sha256", task.sha256)
            .put("totalBytes", task.totalBytes)
            .put("downloadedBytes", task.downloadedBytes)
            .put("bytesPerSecond", task.bytesPerSecond)
            .put("etaSeconds", task.etaSeconds)
            .put("status", task.status.name)
            .put("error", task.error)
            .put("updatedAt", task.updatedAt)
            .put(
                "parts",
                JSONArray().apply {
                    task.parts.forEach { part ->
                        put(
                            JSONObject()
                                .put("downloadUrl", part.downloadUrl)
                                .put("sizeBytes", part.sizeBytes)
                                .put("sha256", part.sha256)
                        )
                    }
                }
            )
        temporary.writeText(json.toString())
        if (!temporary.renameTo(target)) {
            temporary.copyTo(target, overwrite = true)
            temporary.delete()
        }
    }

    private fun key(packId: String, serial: String): String {
        val digest = MessageDigest.getInstance("SHA-256")
            .digest("$packId|${serial.uppercase(Locale.US)}".toByteArray())
        return digest.take(16).joinToString("") { byte -> "%02x".format(byte.toInt() and 0xff) }
    }

    private fun root(context: Context): File = File(context.noBackupFilesDir, ROOT).apply { mkdirs() }
    private fun directory(context: Context, key: String): File = File(root(context), key)
}

internal fun formatDownloadBytes(bytes: Long): String {
    if (bytes < 1024L) return "$bytes B"
    val kb = bytes / 1024.0
    if (kb < 1024.0) return String.format(Locale.getDefault(), "%.1f KB", kb)
    val mb = kb / 1024.0
    if (mb < 1024.0) return String.format(Locale.getDefault(), "%.1f MB", mb)
    return String.format(Locale.getDefault(), "%.2f GB", mb / 1024.0)
}

internal fun formatDownloadDuration(seconds: Long): String {
    val safe = seconds.coerceAtLeast(0L)
    val hours = safe / 3600L
    val minutes = (safe % 3600L) / 60L
    val remainingSeconds = safe % 60L
    return when {
        hours > 0L -> String.format(Locale.getDefault(), "%d:%02d:%02d", hours, minutes, remainingSeconds)
        else -> String.format(Locale.getDefault(), "%d:%02d", minutes, remainingSeconds)
    }
}

private fun ByteArray.toHex(): String =
    joinToString("") { byte -> "%02X".format(byte.toInt() and 0xff) }

private class PermanentDownloadException(message: String) : IOException(message)
