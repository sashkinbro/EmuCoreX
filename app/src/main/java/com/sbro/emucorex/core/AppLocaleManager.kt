package com.sbro.emucorex.core

import android.content.Context
import android.content.res.Configuration
import android.os.LocaleList
import com.sbro.emucorex.data.AppPreferences
import java.util.Locale

object AppLocaleManager {
    fun wrap(base: Context): Context {
        val languageTag = normalizeLanguageTag(AppPreferences(base).getStoredLanguageTagSync())
        if (languageTag.isNullOrBlank()) return base

        val locale = Locale.forLanguageTag(languageTag)
        Locale.setDefault(locale)

        val configuration = Configuration(base.resources.configuration)
        configuration.setLocale(locale)
        configuration.setLocales(LocaleList(locale))
        return base.createConfigurationContext(configuration)
    }

    /**
     * Context whose resources use the app-selected language. The application context carries the
     * system locale, so text built outside the Activity (native callbacks) must go through this.
     */
    fun localized(context: Context?): Context? {
        if (context == null) return null
        return runCatching { wrap(context.applicationContext ?: context) }.getOrDefault(context)
    }

    private fun normalizeLanguageTag(tag: String?): String? {
        return when (tag?.trim()) {
            "zh-TW", "zh_HK", "zh-HK", "zh-Hant", "zh-Hant-TW" -> "zh"
            // Android versions before V keep Indonesian under the legacy ISO code "in".
            // Android V+ canonicalizes it back to "id", so this works on both paths.
            "id", "id-ID", "in", "in-ID" -> "in"
            "fa-IR" -> "fa"
            else -> tag
        }
    }
}
