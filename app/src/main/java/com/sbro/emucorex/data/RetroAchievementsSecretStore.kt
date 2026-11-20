package com.sbro.emucorex.data

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.security.KeyStore
import java.util.concurrent.ConcurrentHashMap
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * Keeps the RetroAchievements token and the remembered password encrypted at rest with an
 * app-owned Keystore key (same pattern as the Discord token store). Values are cached in memory
 * so native callbacks never pay for a Keystore round trip.
 */
internal class RetroAchievementsSecretStore(context: Context) {
    private val preferences = context.applicationContext.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)

    fun load(name: String): String? {
        cache[name]?.let { return it.takeIf(String::isNotEmpty) }
        val encrypted = preferences.getString("${name}_v", null)?.let(::decode)
        val iv = preferences.getString("${name}_iv", null)?.let(::decode)
        if (encrypted == null || iv == null) {
            cache[name] = ""
            return null
        }
        val value = runCatching {
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.DECRYPT_MODE, getOrCreateKey(), GCMParameterSpec(TAG_BITS, iv))
            cipher.doFinal(encrypted).toString(Charsets.UTF_8)
        }.getOrElse {
            // Key lost (backup restore, keystore reset): the secret is unrecoverable.
            remove(name)
            return null
        }
        cache[name] = value
        return value.takeIf(String::isNotEmpty)
    }

    fun save(name: String, value: String): Boolean {
        if (value.isEmpty()) return false
        val saved = runCatching {
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.ENCRYPT_MODE, getOrCreateKey())
            val encrypted = cipher.doFinal(value.toByteArray(Charsets.UTF_8))
            preferences.edit()
                .putString("${name}_iv", encode(cipher.iv))
                .putString("${name}_v", encode(encrypted))
                .commit()
        }.getOrDefault(false)
        if (saved) cache[name] = value
        return saved
    }

    fun remove(name: String) {
        preferences.edit().remove("${name}_iv").remove("${name}_v").commit()
        cache[name] = ""
    }

    private fun getOrCreateKey(): SecretKey {
        val keyStore = KeyStore.getInstance(KEYSTORE).apply { load(null) }
        (keyStore.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }
        return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE).run {
            init(
                KeyGenParameterSpec.Builder(
                    KEY_ALIAS,
                    KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT
                )
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setRandomizedEncryptionRequired(true)
                    .build()
            )
            generateKey()
        }
    }

    private fun encode(value: ByteArray): String = Base64.encodeToString(value, Base64.NO_WRAP)

    private fun decode(value: String): ByteArray = Base64.decode(value, Base64.NO_WRAP)

    companion object {
        const val TOKEN = "token"
        const val PASSWORD = "password"
        private const val PREFERENCES = "retroachievements_secure"
        private const val KEY_ALIAS = "emucorex_retroachievements_secrets"
        private const val KEYSTORE = "AndroidKeyStore"
        private const val TRANSFORMATION = "AES/GCM/NoPadding"
        private const val TAG_BITS = 128

        // "" marks a known-empty slot so absent values do not hit SharedPreferences repeatedly.
        private val cache = ConcurrentHashMap<String, String>()
    }
}
