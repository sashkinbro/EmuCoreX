package com.sbro.emucorex.core.utils

import android.util.Log

object DiscordBridge {
    private const val TAG = "DiscordBridge"

    @JvmStatic
    external fun nativeConfigure(applicationId: Long, scheme: String?, displayName: String?, imageKey: String?)

    @JvmStatic
    external fun nativeProvideStoredToken(accessToken: String?, refreshToken: String?, tokenType: String?, expiresAt: Long, scope: String?)

    @JvmStatic
    external fun nativeBeginAuthorize()

    @JvmStatic
    external fun nativeSetAppForeground(isForeground: Boolean)

    @JvmStatic
    external fun nativePollCallbacks()

    @JvmStatic
    external fun nativeClearTokens()

    @JvmStatic
    external fun nativeIsLoggedIn(): Boolean

    @JvmStatic
    external fun nativeIsClientReady(): Boolean

    @JvmStatic
    external fun nativeConsumeLastError(): String?

    @JvmStatic
    fun onStateChanged(state: Int) {
        Log.v(TAG, "Discord state changed: $state")
    }
}
