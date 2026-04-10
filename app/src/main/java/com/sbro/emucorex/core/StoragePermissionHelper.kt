package com.sbro.emucorex.core

import android.Manifest
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.Settings
import androidx.core.content.ContextCompat
import android.content.pm.PackageManager

object StoragePermissionHelper {

    fun needsStoragePermissionStep(rawGamePath: String?): Boolean {
        return !rawGamePath.isNullOrBlank() && !rawGamePath.startsWith("content://")
    }

    fun hasGameLibraryAccess(context: Context, rawGamePath: String?): Boolean {
        if (rawGamePath.isNullOrBlank()) return false
        if (rawGamePath.startsWith("content://")) return true
        if (!DocumentPathResolver.isScopedStorageExternalPath(rawGamePath)) return true

        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Environment.isExternalStorageManager()
        } else {
            ContextCompat.checkSelfPermission(
                context,
                Manifest.permission.READ_EXTERNAL_STORAGE
            ) == PackageManager.PERMISSION_GRANTED
        }
    }

    fun createStorageAccessIntent(context: Context): Intent {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val appSpecificIntent = Intent(
                Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:${context.packageName}")
            ).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            if (appSpecificIntent.resolveActivity(context.packageManager) != null) {
                appSpecificIntent
            } else {
                Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
        } else {
            Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
                data = Uri.parse("package:${context.packageName}")
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
        }
    }
}
