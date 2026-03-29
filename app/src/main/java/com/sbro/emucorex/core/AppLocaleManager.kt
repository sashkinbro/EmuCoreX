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

    private fun normalizeLanguageTag(tag: String?): String? {
        return when (tag?.trim()) {
            "zh-TW", "zh_HK", "zh-HK", "zh-Hant", "zh-Hant-TW" -> "zh"
            else -> tag
        }
    }
}
