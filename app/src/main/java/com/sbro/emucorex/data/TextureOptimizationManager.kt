package com.sbro.emucorex.data

import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.Uri
import androidx.core.app.NotificationCompat
import androidx.work.CoroutineWorker
import androidx.work.Data
import androidx.work.ExistingWorkPolicy
import androidx.work.ForegroundInfo
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.WorkerParameters
import com.sbro.emucorex.R
import com.sbro.emucorex.core.BackupSessionGate
import com.sbro.emucorex.core.EmulatorStorage
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import java.io.File
import java.util.UUID

data class TextureOptimizationState(
    val id: String = "",
    val phase: String = "",
    val completed: Int = 0,
    val total: Int = 0,
    val optimized: Int = 0,
    val skipped: Int = 0,
    val elapsedMs: Long = 0,
    val error: String = ""
) {
    val active get() = phase in setOf("queued", "preparing", "optimizing", "waiting", "installing", "downloading", "waiting_network", "verifying")
    val etaSeconds get() = if (completed > 0 && completed < total) elapsedMs * (total - completed) / completed / 1000 else 0
}

/** One operation boundary shared by manual imports, installed packs and downloads. */
internal class TextureGameRequested : CancellationException("Game startup takes priority")

object TextureInstallCoordinator {
    val mutex = Mutex()
}

class TextureOptimizationManager(context: Context) {
    private val context = context.applicationContext
    private val prefs = this.context.getSharedPreferences("texture_optimization", Context.MODE_PRIVATE)
    var block: Int
        get() = prefs.getInt("block", -1).takeIf { it in -1..13 } ?: -1
        set(value) { require(value in -1..13); prefs.edit().putInt("block", value).apply() }

    fun state(): TextureOptimizationState = synchronized(lock) {
        TextureOptimizationState(
            prefs.getString("id", "").orEmpty(), prefs.getString("phase", "").orEmpty(),
            prefs.getInt("completed", 0), prefs.getInt("total", 0), prefs.getInt("optimized", 0),
            prefs.getInt("skipped", 0), prefs.getLong("elapsed", 0), prefs.getString("error", "").orEmpty()
        )
    }

    fun publish(state: TextureOptimizationState) = synchronized(lock) {
        prefs.edit().putString("id", state.id).putString("phase", state.phase)
            .putInt("completed", state.completed).putInt("total", state.total)
            .putInt("optimized", state.optimized).putInt("skipped", state.skipped)
            .putLong("elapsed", state.elapsedMs).putString("error", state.error).commit()
    }

    internal fun publishFromWorker(state: TextureOptimizationState) = synchronized(lock) {
        val current = this.state()
        if (current.id != state.id || current.phase in setOf("paused", "cancelled")) throw CancellationException()
        publish(state)
    }

    fun import(uri: Uri, quality: Int = block): Boolean = enqueue(uri.toString(), "", quality)
    fun optimize(serial: String, quality: Int = block): Boolean = enqueue("", serial, quality)

    private fun enqueue(uri: String, serial: String, quality: Int = block): Boolean = synchronized(lock) {
        if (state().active || state().phase in setOf("paused", "failed")) return false
        if (TextureDownloadManager(context).tasks().any { it.status.isActive || it.status == TextureDownloadStatus.PAUSED }) return false
        if (uri.isNotEmpty()) runCatching {
            context.contentResolver.takePersistableUriPermission(Uri.parse(uri), Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        val id = UUID.randomUUID().toString()
        val displayName = if (uri.isNotEmpty()) runCatching {
            context.contentResolver.query(Uri.parse(uri), arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                if (it.moveToFirst()) it.getString(0) else null
            }
        }.getOrNull() else null
        prefs.edit().putString("uri", uri).putString("serial", serial).putString("display_name", displayName)
            .putInt("job_block", quality)
            .putString("root", EmulatorStorage.texturesDir(context, AppPreferences(context).getEmulatorDataPathSync()).absolutePath).commit()
        publish(TextureOptimizationState(id, "queued"))
        schedule(id)
        true
    }

    private fun schedule(id: String) {
        val request = OneTimeWorkRequestBuilder<TextureOptimizationWorker>()
            .setInputData(Data.Builder().putString("id", id).putString("uri", uri()).putString("serial", serial())
                .putInt("block", jobBlock()).putString("display_name", displayName()).putString("root", root()).build()).build()
        WorkManager.getInstance(context).enqueueUniqueWork(WORK_NAME, ExistingWorkPolicy.REPLACE, request)
    }

    fun pause(): Unit = synchronized(lock) {
        val state = state()
        if (!state.active) return
        publish(state.copy(phase = "paused"))
        WorkManager.getInstance(context).cancelUniqueWork(WORK_NAME)
    }

    fun resume(): Unit = synchronized(lock) {
        val state = state()
        if (state.phase !in setOf("paused", "failed")) return
        publish(state.copy(phase = "queued", error = ""))
        schedule(state.id)
    }

    fun cancel() = synchronized(lock) {
        val current = state()
        publish(current.copy(phase = "cancelled"))
        WorkManager.getInstance(context).cancelUniqueWork(WORK_NAME)
        if (current.id.isNotEmpty()) {
            WorkManager.getInstance(context).enqueue(OneTimeWorkRequestBuilder<TextureOptimizationCleanupWorker>()
                .setInputData(Data.Builder().putString("id", current.id).putString("root", root()).build()).build())
        }
    }

    internal fun root() = prefs.getString("root", null)
    fun dismissResult() = synchronized(lock) {
        if (state().phase in setOf("completed", "cancelled")) publish(TextureOptimizationState())
    }
    fun dismissDownload(task: TextureDownloadTask) {
        prefs.edit().putString("dismissed_download", "${task.key}:${task.updatedAt}").apply()
    }
    fun isDownloadDismissed(task: TextureDownloadTask) =
        prefs.getString("dismissed_download", "") == "${task.key}:${task.updatedAt}"
    internal fun uri() = prefs.getString("uri", "").orEmpty()
    internal fun serial() = prefs.getString("serial", "").orEmpty()
    internal fun jobBlock() = prefs.getInt("job_block", -1)
    internal fun displayName() = prefs.getString("display_name", null)

    companion object {
        private val lock = Any()
        private const val WORK_NAME = "texture_optimization"
    }
}

class TextureOptimizationCleanupWorker(context: Context, parameters: WorkerParameters) : CoroutineWorker(context, parameters) {
    override suspend fun doWork(): Result = withContext(Dispatchers.IO) {
        val id = inputData.getString("id").orEmpty()
        if (!id.matches(Regex("[A-Za-z0-9_-]{1,128}"))) return@withContext Result.failure()
        TextureInstallCoordinator.mutex.withLock {
            val root = inputData.getString("root")?.let(::File)
            if (inputData.getBoolean("download", false)) {
                val current = TextureDownloadManager(applicationContext).tasks().firstOrNull { it.key == id }
                if (current != null && current.status != TextureDownloadStatus.CANCELLED) return@withLock
            }
            TexturePackRepository(applicationContext, AppPreferences(applicationContext), root).discardOperation(id)
            File(applicationContext.noBackupFilesDir, "texture-import-$id.zip").delete()
            File(applicationContext.noBackupFilesDir, "texture-import-$id.zip.part").delete()
        }
        Result.success()
    }
}

class TextureOptimizationWorker(context: Context, parameters: WorkerParameters) : CoroutineWorker(context, parameters) {
    override suspend fun doWork(): Result = withContext(Dispatchers.IO) {
        val manager = TextureOptimizationManager(applicationContext)
        val id = inputData.getString("id").orEmpty()
        if (id.isBlank() || manager.state().id != id || manager.state().phase in setOf("paused", "cancelled"))
            return@withContext Result.success()
        val uri = inputData.getString("uri") ?: manager.uri()
        val serial = inputData.getString("serial") ?: manager.serial()
        val block = inputData.getInt("block", manager.jobBlock())
        val displayName = inputData.getString("display_name") ?: manager.displayName()
        val root = (inputData.getString("root") ?: manager.root())?.let(::File)
        val coroutine = currentCoroutineContext()
        val previous = manager.state()
        val start = android.os.SystemClock.elapsedRealtime()
        var lastReport = 0L
        try {
            setForeground(notification())
            TextureInstallCoordinator.mutex.withLock {
                BackupSessionGate.awaitStopped(onWaiting = {
                    manager.publishFromWorker(manager.state().copy(id = id, phase = "waiting"))
                }) {
                    coroutine.ensureActive()
                    manager.publishFromWorker(manager.state().copy(id = id, phase = "preparing"))
                    val repository = TexturePackRepository(applicationContext, AppPreferences(applicationContext), root)
                    val report: (TextureOptimizationProgress) -> Unit = { progress ->
                        coroutine.ensureActive()
                        if (BackupSessionGate.gameBusy) throw TextureGameRequested()
                        if (manager.state().phase in setOf("paused", "cancelled")) throw CancellationException()
                        val now = android.os.SystemClock.elapsedRealtime()
                        if (now - lastReport >= 350 || progress.completed == progress.total && progress.total > 0) {
                            lastReport = now
                            manager.publishFromWorker(TextureOptimizationState(id, if (progress.total > 0) "optimizing" else "preparing",
                                if (progress.total == previous.total || progress.total == 0) maxOf(previous.completed, progress.completed) else progress.completed,
                                if (progress.total == 0) previous.total else progress.total,
                                maxOf(previous.optimized, progress.optimized), maxOf(previous.skipped, progress.skipped), previous.elapsedMs + now - start))
                            applicationContext.getSystemService(NotificationManager::class.java)
                                .notify(8302, notification(manager.state()).notification)
                        }
                    }
                    val result = if (uri.isNotEmpty()) {
                        // Keep a private input snapshot: the selected document may disappear or
                        // change while a paused job is waiting to resume.
                        val snapshot = File(applicationContext.noBackupFilesDir, "texture-import-$id.zip")
                        if (!snapshot.isFile) {
                            val part = File(snapshot.parentFile, snapshot.name + ".part")
                            applicationContext.contentResolver.openInputStream(Uri.parse(uri)).use { input ->
                                requireNotNull(input) { "Could not open texture archive" }
                                part.outputStream().use { output ->
                                    val buffer = ByteArray(256 * 1024)
                                    var bytes = 0L
                                    while (true) {
                                        coroutine.ensureActive()
                                        if (BackupSessionGate.gameBusy) throw TextureGameRequested()
                                        val count = input.read(buffer)
                                        if (count < 0) break
                                        bytes += count
                                        require(bytes <= 24L * 1024 * 1024 * 1024 && part.usableSpace > 64L * 1024 * 1024) { "Not enough space for texture archive" }
                                        output.write(buffer, 0, count)
                                    }
                                }
                            }
                            check(part.renameTo(snapshot)) { "Could not prepare texture archive" }
                        }
                        snapshot.inputStream().use { repository.importPackZip(it, displayName, astcBlock = block, operationId = id, progress = report) }
                    } else repository.optimizeInstalled(serial, block, id, report)
                    coroutine.ensureActive()
                    if (!result.success) error(result.error.ifBlank { "Texture archive could not be installed" })
                    AppPreferences(applicationContext).setTextureReplacementsEnabled(true)
                    manager.publishFromWorker(manager.state().copy(id = id, phase = "completed", optimized = result.optimizedFiles,
                        skipped = result.skippedFiles, elapsedMs = previous.elapsedMs + android.os.SystemClock.elapsedRealtime() - start))
                    File(applicationContext.noBackupFilesDir, "texture-import-$id.zip").delete()
                }
            }
            Result.success()
        } catch (waiting: TextureGameRequested) {
            manager.publishFromWorker(manager.state().copy(id = id, phase = "waiting"))
            Result.retry()
        } catch (cancelled: CancellationException) { throw cancelled }
        catch (error: Exception) {
            if (manager.state().id == id && manager.state().phase !in setOf("paused", "cancelled"))
                manager.publish(manager.state().copy(phase = "failed", error = error.message.orEmpty()))
            Result.failure()
        }
    }

    private fun notification(state: TextureOptimizationState = TextureOptimizationState()): ForegroundInfo {
        val channel = "texture_optimization"
        applicationContext.getSystemService(NotificationManager::class.java).createNotificationChannel(
            NotificationChannel(channel, applicationContext.getString(R.string.texture_manager_title), NotificationManager.IMPORTANCE_LOW))
        val notification = NotificationCompat.Builder(applicationContext, channel)
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentTitle(applicationContext.getString(R.string.texture_manager_title))
            .setContentText(if (state.total > 0) applicationContext.getString(R.string.texture_opt_progress,
                state.completed, state.total, (100L * state.completed / state.total).toInt())
                else applicationContext.getString(R.string.texture_opt_preparing))
            .setOnlyAlertOnce(true).setOngoing(true).setProgress(state.total, state.completed, state.total == 0).build()
        return ForegroundInfo(8302, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
    }
}
