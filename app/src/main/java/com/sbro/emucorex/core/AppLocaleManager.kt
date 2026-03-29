package com.sbro.emucorex.core

import android.content.Context
import android.content.res.Configuration
import android.os.LocaleList
import com.sbro.emucorex.data.AppPreferences
import java.util.Locale

object AppLocaleManager {
    fun wrap(base: Context): Context {
        val languageTag = AppPreferences(base).getStoredLanguageTagSync()
        if (languageTag.isNullOrBlank()) return base

        val locale = Locale.forLanguageTag(languageTag)
        Locale.setDefault(locale)

        val configuration = Configuration(base.resources.configuration)
        configuration.setLocale(locale)
        configuration.setLocales(LocaleList(locale))
        return base.createConfigurationContext(configuration)
    }
}
