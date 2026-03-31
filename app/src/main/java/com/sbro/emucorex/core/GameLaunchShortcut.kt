package com.sbro.emucorex.core

import android.content.Context
import android.content.Intent
import androidx.core.content.pm.ShortcutInfoCompat
import androidx.core.content.pm.ShortcutManagerCompat
import androidx.core.graphics.drawable.IconCompat
import com.sbro.emucorex.MainActivity
import com.sbro.emucorex.R
import com.sbro.emucorex.data.GameItem

object GameLaunchShortcut {
    const val EXTRA_GAME_PATH = "com.sbro.emucorex.extra.GAME_PATH"
    const val EXTRA_SAVE_SLOT = "com.sbro.emucorex.extra.SAVE_SLOT"

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
            action = Intent.ACTION_VIEW
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
}
