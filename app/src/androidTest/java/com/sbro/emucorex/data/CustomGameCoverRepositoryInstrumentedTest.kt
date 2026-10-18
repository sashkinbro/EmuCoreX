package com.sbro.emucorex.data

import android.content.Context
import android.content.ContextWrapper
import android.net.Uri
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.File
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class CustomGameCoverRepositoryInstrumentedTest {
    private fun withIsolatedCovers(block: (Context, File) -> Unit) {
        val base = ApplicationProvider.getApplicationContext<Context>()
        AppPreferences(base).getGameSerialOverridesSync()
        val root = File(base.cacheDir, "custom-cover-reset-${System.nanoTime()}").apply { mkdirs() }
        val context = object : ContextWrapper(base) {
            override fun getApplicationContext(): Context = this
            override fun getFilesDir(): File = File(root, "files").apply { mkdirs() }
            override fun getCacheDir(): File = File(root, "cache").apply { mkdirs() }
        }
        try { block(context, root) } finally { root.deleteRecursively() }
    }

    @Test
    fun resetOneGameRemovesEverySavedVariantAndKeepsOtherGamesAndOriginals() = withIsolatedCovers { context, root ->
        val first = CustomGameCoverRepository(context)
        val second = CustomGameCoverRepository(context)
        val original = File(root, "original.png").apply { writeText("original artwork") }
        val coverA = first.saveCustomCover("/games/a.iso", Uri.fromFile(original))!!
        val coverB = second.saveCustomCover("/games/b.iso", Uri.fromFile(original))!!
        val oldVariant = File(File(coverA).parentFile, File(coverA).nameWithoutExtension + ".jpg")
            .apply { writeText("old version") }

        assertTrue(second.removeCustomCover("/games/a.iso"))
        assertNull(first.findCustomCoverPath("/games/a.iso"))
        assertFalse(File(coverA).exists())
        assertFalse(oldVariant.exists())
        assertEquals(coverB, first.findCustomCoverPath("/games/b.iso"))
        assertEquals("original artwork", original.readText())
        assertTrue(first.removeCustomCover("/games/a.iso"))
    }

    @Test
    fun resetAllKeepsDownloadedCoversGameFilesAndOriginalImages() = withIsolatedCovers { context, root ->
        val repository = CustomGameCoverRepository(context)
        val original = File(root, "source.webp").apply { writeText("original") }
        val game = File(root, "game.iso").apply { writeText("game data") }
        val cached = File(context.cacheDir, "game-covers/default.png").apply {
            parentFile!!.mkdirs(); writeText("default artwork")
        }
        repository.saveCustomCover(game.path, Uri.fromFile(original))!!
        repository.saveCustomCover("content://games/second", Uri.fromFile(original))!!

        assertTrue(repository.resetAllCustomCovers())
        assertNull(repository.findCustomCoverPath(game.path))
        assertNull(repository.findCustomCoverPath("content://games/second"))
        assertEquals("original", original.readText())
        assertEquals("game data", game.readText())
        assertEquals("default artwork", cached.readText())
        assertTrue(repository.resetAllCustomCovers())
    }
    @Test
    fun freshLibraryCacheDropsDeletedCustomPathsButKeepsDefaultCovers() = withIsolatedCovers { context, root ->
        val covers = CustomGameCoverRepository(context)
        val source = File(root, "source.png").apply { writeText("custom") }
        val path = File(root, "game.iso").path
        val custom = covers.saveCustomCover(path, Uri.fromFile(source))!!
        val defaultCover = File(context.cacheDir, "game-covers/default.png").apply {
            parentFile!!.mkdirs(); writeText("default")
        }
        val cache = GameLibraryCacheRepository(context)
        cache.save(root.path, listOf(
            GameItem("Cover test A", path, "game.iso", 1, 0, custom),
            GameItem("Cover test B", "$path-other", "other.iso", 1, 0, defaultCover.path)
        ))
        assertTrue(covers.resetAllCustomCovers())
        val games = cache.loadSnapshot(root.path).games
        assertEquals(2, games.size)
        assertNull(games.first { it.path == path }.coverArtPath)
        assertEquals(defaultCover.path, games.first { it.path != path }.coverArtPath)
    }

    @Test
    fun failedDeletionReportsFailureAndAllowsRetry() = withIsolatedCovers { context, root ->
        val repository = CustomGameCoverRepository(context)
        val source = File(root, "source.png").apply { writeText("custom artwork") }
        val cover = File(repository.saveCustomCover("/games/retry.iso", Uri.fromFile(source))!!)
        val directory = cover.parentFile!!
        try {
            assertTrue(directory.setWritable(false, false))
            assertFalse(repository.resetAllCustomCovers())
            assertEquals(cover.path, repository.findCustomCoverPath("/games/retry.iso"))
            assertEquals("custom artwork", source.readText())
        } finally {
            assertTrue(directory.setWritable(true, true))
        }
        assertTrue(repository.removeCustomCover("/games/retry.iso"))
        assertNull(repository.findCustomCoverPath("/games/retry.iso"))
    }

}
