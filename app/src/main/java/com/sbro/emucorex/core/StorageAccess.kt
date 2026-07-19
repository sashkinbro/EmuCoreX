package com.sbro.emucorex.core

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.util.Log

object StorageAccess {
    private const val TAG = "StorageAccess"

    fun takePersistableReadPermission(context: Context, uri: Uri): Boolean {
        return takePersistablePermission(context, uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }

    fun takePersistableReadWritePermission(context: Context, uri: Uri): Boolean {
        return takePersistablePermission(
            context,
            uri,
            Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
        )
    }

    fun releasePersistedPermission(context: Context, rawPath: String?) {
        if (rawPath.isNullOrBlank() || !rawPath.startsWith("content://")) return
        val uri = runCatching { Uri.parse(rawPath) }.getOrNull() ?: return
        val permission = runCatching {
            context.contentResolver.persistedUriPermissions.firstOrNull { it.uri == uri }
        }.getOrNull() ?: return
        val flags = (if (permission.isReadPermission) Intent.FLAG_GRANT_READ_URI_PERMISSION else 0) or
            (if (permission.isWritePermission) Intent.FLAG_GRANT_WRITE_URI_PERMISSION else 0)
        if (flags == 0) return
        runCatching {
            context.contentResolver.releasePersistableUriPermission(uri, flags)
        }.onFailure { error ->
            Log.w(TAG, "Unable to release persisted URI permission for $uri", error)
        }
    }

    private fun takePersistablePermission(context: Context, uri: Uri, requestedFlags: Int): Boolean {
        val resolver = context.contentResolver
        val attempts = buildList {
            add(requestedFlags)
            if (requestedFlags and Intent.FLAG_GRANT_READ_URI_PERMISSION != 0) {
                add(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            if (requestedFlags and Intent.FLAG_GRANT_WRITE_URI_PERMISSION != 0) {
                add(Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
            }
        }.distinct().filter { it != 0 }

        for (flags in attempts) {
            runCatching {
                resolver.takePersistableUriPermission(uri, flags)
            }.onFailure { error ->
                Log.w(TAG, "Persistable URI permission failed for $uri flags=$flags", error)
            }
            if (hasPersistedPermission(
                    context = context,
                    uri = uri,
                    requireRead = requestedFlags and Intent.FLAG_GRANT_READ_URI_PERMISSION != 0,
                    requireWrite = requestedFlags and Intent.FLAG_GRANT_WRITE_URI_PERMISSION != 0
                )
            ) {
                return true
            }
        }

        return hasPersistedPermission(
            context = context,
            uri = uri,
            requireRead = requestedFlags and Intent.FLAG_GRANT_READ_URI_PERMISSION != 0,
            requireWrite = requestedFlags and Intent.FLAG_GRANT_WRITE_URI_PERMISSION != 0
        )
    }

    private fun hasPersistedPermission(
        context: Context,
        uri: Uri,
        requireRead: Boolean,
        requireWrite: Boolean
    ): Boolean = runCatching {
        context.contentResolver.persistedUriPermissions.any { permission ->
            permission.uri == uri &&
                (!requireRead || permission.isReadPermission) &&
                (!requireWrite || permission.isWritePermission)
        }
    }.onFailure { error ->
        Log.w(TAG, "Unable to inspect persisted URI permissions for $uri", error)
    }.getOrDefault(false)
}
