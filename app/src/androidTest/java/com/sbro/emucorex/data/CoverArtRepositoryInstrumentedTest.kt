package com.sbro.emucorex.data

import android.content.Context
import android.graphics.Bitmap
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.File
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class CoverArtRepositoryInstrumentedTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()
    private val repository = CoverArtRepository(context)

    @Test
    fun missingAndCorruptManagedCoversRequireRefresh() {
        val cacheDirectory = File(context.cacheDir, "game-covers").apply { mkdirs() }
        val missing = File(cacheDirectory, "TEST-MISSING.jpg")
        val corrupt = File(cacheDirectory, "TEST-CORRUPT.jpg")
        val valid = File(cacheDirectory, "TEST-VALID.png")
        try {
            missing.delete()
            corrupt.writeText("not an image")
            val bitmap = Bitmap.createBitmap(2, 2, Bitmap.Config.ARGB_8888)
            try {
                valid.outputStream().use { output -> bitmap.compress(Bitmap.CompressFormat.PNG, 100, output) }
            } finally {
                bitmap.recycle()
            }

            assertTrue(repository.isMissingManagedCover(missing.absolutePath))
            assertTrue(repository.isMissingManagedCover(corrupt.absolutePath))
            assertFalse(repository.isMissingManagedCover(valid.absolutePath))
            assertFalse(repository.isMissingManagedCover("content://custom/cover"))
        } finally {
            corrupt.delete()
            valid.delete()
        }
    }

    @Test
    fun clearTemporaryCachesPreservesCustomCoversAndNotifiesRevision() = runBlocking {
        val downloaded = File(context.cacheDir, "game-covers/TEST-CACHED.jpg").apply {
            parentFile?.mkdirs()
            writeText("temporary")
        }
        val remote = File(context.cacheDir, "remote-image-cache/TEST-REMOTE.jpg").apply {
            parentFile?.mkdirs()
            writeText("temporary")
        }
        val custom = File(context.filesDir, "library/custom-game-covers/TEST-CUSTOM.jpg").apply {
            parentFile?.mkdirs()
            writeText("user artwork")
        }
        try {
            val before = AppPreferences(context).coverCacheRevision.first()
            val result = repository.clearAllTemporaryImageCaches()
            AppPreferences(context).notifyCoverCacheCleared()
            val after = AppPreferences(context).coverCacheRevision.first()

            assertTrue(result.deletedFiles >= 2)
            assertFalse(downloaded.exists())
            assertFalse(remote.exists())
            assertTrue(custom.exists())
            assertTrue(after > before)
        } finally {
            downloaded.delete()
            remote.delete()
            custom.delete()
        }
    }
}
