package com.sbro.emucorex.data

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CoverCachePolicyTest {

    @Test
    fun isPathInside_acceptsOnlyFilesBelowManagedDirectory() {
        val parent = Files.createTempDirectory("cover-cache-policy").toFile()
        try {
            val cache = File(parent, "game-covers").apply { mkdirs() }
            val cover = File(cache, "SLUS-20312.jpg").apply { writeText("test") }

            assertTrue(CoverCachePolicy.isPathInside(cover.absolutePath, cache))
            assertFalse(CoverCachePolicy.isPathInside(cache.absolutePath, cache))
            assertFalse(CoverCachePolicy.isPathInside(File(parent, "custom.jpg").absolutePath, cache))
        } finally {
            parent.deleteRecursively()
        }
    }

    @Test
    fun isPathInside_rejectsSiblingWithMatchingPrefixAndRemoteUris() {
        val parent = Files.createTempDirectory("cover-cache-policy").toFile()
        try {
            val cache = File(parent, "game-covers").apply { mkdirs() }
            val sibling = File(parent, "game-covers-user/cover.jpg")

            assertFalse(CoverCachePolicy.isPathInside(sibling.absolutePath, cache))
            assertFalse(CoverCachePolicy.isPathInside("content://covers/custom", cache))
            assertFalse(CoverCachePolicy.isPathInside("https://example.com/cover.jpg", cache))
        } finally {
            parent.deleteRecursively()
        }
    }
}
