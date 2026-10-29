package com.sbro.emucorex.core

import android.content.Intent
import android.net.Uri
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertNotNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(manifest = Config.NONE, sdk = [35])
class GameLaunchShortcutTest {
    @Test fun workerOverridesPreserveExplicitOffAndAreConsumed() {
        val intent = Intent(GameLaunchShortcut.ACTION_LAUNCH_GAME).apply {
            putExtra(GameLaunchShortcut.EXTRA_GAME_PATH, "/games/test.iso")
            putExtra(GameLaunchShortcut.EXTRA_ENABLE_IOP_THREAD, false)
            putExtra(GameLaunchShortcut.EXTRA_ENABLE_VU_AOT, true)
        }
        val request = GameLaunchShortcut.parseLaunchRequest(intent)!!
        assertEquals(false, request.enableIopThread)
        assertEquals(true, request.enableVUAot)
        GameLaunchShortcut.clearLaunchRequest(intent)
        assertEquals(false, intent.hasExtra(GameLaunchShortcut.EXTRA_ENABLE_IOP_THREAD))
        assertEquals(false, intent.hasExtra(GameLaunchShortcut.EXTRA_ENABLE_VU_AOT))
        assertNull(GameLaunchShortcut.parseLaunchRequest(intent))
        val query = Intent(Intent.ACTION_VIEW,
            Uri.parse("emucorex://launch?gamePath=%2Fgames%2Ftest.iso&enableIopThread=true&enableVUAot=false"))
        assertEquals(true, GameLaunchShortcut.parseLaunchRequest(query)?.enableIopThread)
        assertEquals(false, GameLaunchShortcut.parseLaunchRequest(query)?.enableVUAot)
    }

    @Test
    fun consumedGameIntentsCannotBeLaunchedAgain() {
        val paths = listOf(
            "content://games/document/game.iso",
            "file:///storage/emulated/0/game.iso",
            "emucorex://launch?gamePath=%2Fgames%2Fgame.iso"
        )
        for (path in paths) {
            val intent = Intent(Intent.ACTION_VIEW, Uri.parse(path))
            val request = GameLaunchShortcut.parseLaunchRequest(intent)
            assertEquals(if (path.startsWith("emucorex:")) "/games/game.iso" else path, request?.gamePath)
            GameLaunchShortcut.clearLaunchRequest(intent)
            assertNull("Consumed request must not be parsed again: $path", GameLaunchShortcut.parseLaunchRequest(intent))
        }
    }

    @Test
    fun aNewIntentCanLaunchAfterThePreviousRequestIsConsumed() {
        val first = Intent(GameLaunchShortcut.ACTION_LAUNCH_GAME).apply {
            putExtra(GameLaunchShortcut.EXTRA_GAME_PATH, "/games/first.iso")
        }
        assertEquals("/games/first.iso", GameLaunchShortcut.parseLaunchRequest(first)?.gamePath)
        GameLaunchShortcut.clearLaunchRequest(first)
        assertNull(GameLaunchShortcut.parseLaunchRequest(first))

        val second = Intent(GameLaunchShortcut.ACTION_LAUNCH_GAME).apply {
            putExtra(GameLaunchShortcut.EXTRA_GAME_PATH, "/games/second.iso")
        }
        assertNotNull(GameLaunchShortcut.parseLaunchRequest(second))
        assertEquals("/games/second.iso", GameLaunchShortcut.parseLaunchRequest(second)?.gamePath)
    }
}
