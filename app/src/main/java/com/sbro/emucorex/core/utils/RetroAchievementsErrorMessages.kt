package com.sbro.emucorex.core.utils

import android.content.Context
import com.sbro.emucorex.R

/** Maps native rc_client / login error codes and texts to localized messages. */
internal object RetroAchievementsErrorMessages {
    enum class Kind {
        CREDENTIALS,
        TOKEN_EXPIRED,
        NETWORK,
        LOGIN_REQUIRED,
        NO_GAME,
        HARDCORE_DISABLED,
        ACCESS_DENIED,
        SERVER,
        TIMEOUT,
        UNKNOWN
    }

    /**
     * [code] is either a numeric rc_error.h result (negative) or a free-form native message.
     * Unknown values return [Kind.UNKNOWN] so the caller can fall back to the native text.
     */
    fun classify(code: String?): Kind {
        val value = code?.trim().orEmpty()
        if (value.isEmpty()) return Kind.UNKNOWN
        value.toIntOrNull()?.let { number ->
            return when (number) {
                -26, -27 -> Kind.SERVER
                -28 -> Kind.LOGIN_REQUIRED
                -29 -> Kind.NO_GAME
                -30 -> Kind.HARDCORE_DISABLED
                -32 -> Kind.NETWORK
                -33, -36 -> Kind.ACCESS_DENIED
                -34 -> Kind.CREDENTIALS
                -35 -> Kind.TOKEN_EXPIRED
                else -> Kind.UNKNOWN
            }
        }
        val text = value.lowercase()
        return when {
            "token" in text && ("expired" in text || "invalid" in text) -> Kind.TOKEN_EXPIRED
            "invalid credentials" in text || "incorrect" in text || "invalid password" in text ||
                "invalid username" in text || "wrong password" in text -> Kind.CREDENTIALS
            "timed out" in text || "timeout" in text -> Kind.TIMEOUT
            "could not resolve" in text || "no response" in text || "network" in text ||
                "unable to connect" in text || "failed to connect" in text || "no route" in text -> Kind.NETWORK
            "login required" in text || "not logged in" in text -> Kind.LOGIN_REQUIRED
            "no game" in text -> Kind.NO_GAME
            "access denied" in text || "permission" in text || "forbidden" in text -> Kind.ACCESS_DENIED
            "hardcore" in text && "disabled" in text -> Kind.HARDCORE_DISABLED
            else -> Kind.UNKNOWN
        }
    }

    fun stringRes(kind: Kind): Int? = when (kind) {
        Kind.CREDENTIALS -> R.string.ra_err_credentials
        Kind.TOKEN_EXPIRED -> R.string.ra_err_token_expired
        Kind.NETWORK -> R.string.ra_err_network
        Kind.LOGIN_REQUIRED -> R.string.ra_err_login_required
        Kind.NO_GAME -> R.string.ra_err_no_game
        Kind.HARDCORE_DISABLED -> R.string.ra_err_hardcore_disabled
        Kind.ACCESS_DENIED -> R.string.ra_err_access_denied
        Kind.SERVER -> R.string.ra_err_server
        Kind.TIMEOUT -> R.string.ra_err_timeout
        Kind.UNKNOWN -> null
    }

    /** Localized text for [code], else [fallback] (native text), else a generic message. */
    fun resolve(context: Context?, code: String?, fallback: String?): String {
        val res = stringRes(classify(code)) ?: stringRes(classify(fallback))
        val localized = if (res != null && context != null) runCatching { context.getString(res) }.getOrNull() else null
        return localized
            ?: fallback?.takeIf { it.isNotBlank() }
            ?: code?.takeIf { it.isNotBlank() && it.toIntOrNull() == null }
            ?: context?.let { runCatching { it.getString(R.string.ra_err_unavailable) }.getOrNull() }
            ?: "RetroAchievements is unavailable right now."
    }
}
