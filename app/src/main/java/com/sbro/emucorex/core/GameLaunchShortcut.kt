package com.sbro.emucorex.core

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.core.content.pm.ShortcutInfoCompat
import androidx.core.content.pm.ShortcutManagerCompat
import androidx.core.graphics.drawable.IconCompat
import com.sbro.emucorex.MainActivity
import com.sbro.emucorex.R
import com.sbro.emucorex.data.GameItem

object GameLaunchShortcut {
    const val ACTION_LAUNCH_GAME = "com.sbro.emucorex.action.LAUNCH_GAME"
    const val EXTRA_GAME_PATH = "com.sbro.emucorex.extra.GAME_PATH"
    const val EXTRA_SAVE_SLOT = "com.sbro.emucorex.extra.SAVE_SLOT"
    const val EXTRA_BOOT_BIOS = "com.sbro.emucorex.extra.BOOT_BIOS"

    private const val SCHEME = "emucorex"
    private const val HOST = "launch"

    data class LaunchRequest(
        val gamePath: String? = null,
        val saveSlot: Int? = null,
        val bootBios: Boolean = false
    )

    fun requestPinnedShortcut(
        context: Context,
        game: GameItem,
        saveSlot: Int? = null
    ): Boolean {
        val shortcutId = buildString {
            append("game:")
            append(game.path.hashCode())
            saveSlot?.let {
                append(":")
                append(it)
            }
        }
        val launchIntent = Intent(context, MainActivity::class.java).apply {
            action = ACTION_LAUNCH_GAME
            putExtra(EXTRA_GAME_PATH, game.path)
            saveSlot?.let { putExtra(EXTRA_SAVE_SLOT, it) }
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
        }
        val shortcut = ShortcutInfoCompat.Builder(context, shortcutId)
            .setShortLabel(game.title.take(40))
            .setLongLabel(game.title)
            .setIcon(IconCompat.createWithResource(context, R.mipmap.ic_launcher))
            .setIntent(launchIntent)
            .build()
        return ShortcutManagerCompat.requestPinShortcut(context, shortcut, null)
    }

    fun buildLaunchUri(
        gamePath: String? = null,
        saveSlot: Int? = null,
        bootBios: Boolean = false
    ): Uri {
        return Uri.Builder()
            .scheme(SCHEME)
            .authority(HOST)
            .apply {
                gamePath?.let { appendQueryParameter("gamePath", it) }
                saveSlot?.let { appendQueryParameter("saveSlot", it.toString()) }
                if (bootBios) appendQueryParameter("bootBios", "true")
            }
            .build()
    }

    fun parseLaunchRequest(intent: Intent?): LaunchRequest? {
        intent ?: return null
        val data = intent.data
        val gamePathFromData = when {
            data == null -> null
            data.scheme == SCHEME && data.host == HOST -> data.getQueryParameter("gamePath")
            data.scheme == "content" || data.scheme == "file" -> data.toString()
            else -> null
        }
        val gamePath = intent.getStringExtra(EXTRA_GAME_PATH) ?: gamePathFromData
        val saveSlot = when {
            intent.hasExtra(EXTRA_SAVE_SLOT) -> intent.getIntExtra(EXTRA_SAVE_SLOT, -1).takeIf { it >= 0 }
            data?.scheme == SCHEME && data.host == HOST -> data.getQueryParameter("saveSlot")?.toIntOrNull()?.takeIf { it >= 0 }
            else -> null
        }
        val bootBios = intent.getBooleanExtra(EXTRA_BOOT_BIOS, false) ||
            (data?.scheme == SCHEME && data.host == HOST && data.getQueryParameter("bootBios") == "true")
        if (gamePath.isNullOrBlank() && !bootBios) return null
        return LaunchRequest(
            gamePath = gamePath,
            saveSlot = saveSlot,
            bootBios = bootBios
        )
    }

    fun clearLaunchRequest(intent: Intent?) {
        intent ?: return
        intent.removeExtra(EXTRA_GAME_PATH)
        intent.removeExtra(EXTRA_SAVE_SLOT)
        intent.removeExtra(EXTRA_BOOT_BIOS)
        if (intent.data?.scheme == SCHEME && intent.data?.host == HOST) {
            intent.data = null
        }
    }
}
