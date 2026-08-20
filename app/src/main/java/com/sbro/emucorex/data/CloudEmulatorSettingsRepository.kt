package com.sbro.emucorex.data

import android.content.Context
import com.google.android.gms.tasks.Task
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.firestore.DocumentSnapshot
import com.google.firebase.firestore.FieldValue
import com.google.firebase.firestore.FirebaseFirestore
import com.sbro.emucorex.BuildConfig
import kotlinx.coroutines.suspendCancellableCoroutine
import org.json.JSONArray
import org.json.JSONObject
import java.nio.charset.StandardCharsets
import java.util.UUID
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

data class CloudEmulatorProfile(
    val id: String,
    val name: String,
    val sourceDeviceId: String,
    val appVersion: String,
    val coreVersion: String,
    val updatedAtMs: Long?
)

/**
 * Explicit, user-driven backup/restore for portable emulator settings.
 *
 * This intentionally does not auto-merge settings between devices: restoring is an atomic
 * user choice, which avoids a stale device silently overwriting a tuned configuration.
 */
class CloudEmulatorSettingsRepository(context: Context) {
    private val appContext = context.applicationContext
    private val auth = FirebaseAuth.getInstance()
    private val firestore = FirebaseFirestore.getInstance()
    private val preferences = AppPreferences(appContext)
    private val perGameSettings = PerGameSettingsRepository(appContext)

    suspend fun loadProfiles(): List<CloudEmulatorProfile> {
        val uid = auth.currentUser?.uid ?: return emptyList()
        return profileCollection(uid)
            .orderBy(FIELD_UPDATED_AT, com.google.firebase.firestore.Query.Direction.DESCENDING)
            .limit(MAX_PROFILES.toLong())
            .get()
            .await()
            .documents
            .mapNotNull { document -> document.toCloudProfile() }
    }

    suspend fun saveCurrent(name: String, replaceProfileId: String? = null): CloudEmulatorProfile {
        val uid = auth.currentUser?.uid ?: error("Sign in is required")
        val normalizedName = name.trim().replace(Regex("\\s+"), " ").take(MAX_NAME_LENGTH)
        require(normalizedName.isNotEmpty()) { "Profile name is required" }

        val existingProfiles = loadProfiles()
        val profileId = replaceProfileId?.takeIf { candidate ->
            existingProfiles.any { it.id == candidate }
        } ?: UUID.randomUUID().toString()
        if (replaceProfileId == null && existingProfiles.size >= MAX_PROFILES) {
            error("A maximum of $MAX_PROFILES cloud profiles is supported")
        }

        val portable = preferences.exportEmulatorCloudJson()
        val perGame = sanitizePerGameSettings(perGameSettings.exportJson())
        val payloadBytes = portable.toString().toByteArray(StandardCharsets.UTF_8).size +
            perGame.toString().toByteArray(StandardCharsets.UTF_8).size
        require(payloadBytes <= MAX_PAYLOAD_BYTES) { "The emulator profile is too large to sync" }

        val device = ProfileDeviceInfoProvider.current(appContext)
        val document = profileCollection(uid).document(profileId)
        val current = document.get().await()
        val data = mutableMapOf<String, Any>(
            "uid" to uid,
            "profileId" to profileId,
            "name" to normalizedName,
            "schemaVersion" to SCHEMA_VERSION,
            "appVersion" to BuildConfig.VERSION_NAME,
            "coreVersion" to ProfileDeviceInfoProvider.CORE_VERSION,
            "sourceDeviceId" to device.deviceId,
            "portableSettings" to portable.toFirestoreValue(),
            "perGameSettings" to perGame.toFirestoreValue(),
            FIELD_UPDATED_AT to FieldValue.serverTimestamp()
        )
        if (!current.exists()) data[FIELD_CREATED_AT] = FieldValue.serverTimestamp()
        document.set(data, com.google.firebase.firestore.SetOptions.merge()).await()
        return CloudEmulatorProfile(
            id = profileId,
            name = normalizedName,
            sourceDeviceId = device.deviceId,
            appVersion = BuildConfig.VERSION_NAME,
            coreVersion = ProfileDeviceInfoProvider.CORE_VERSION,
            updatedAtMs = System.currentTimeMillis()
        )
    }

    suspend fun restore(profileId: String) {
        val uid = auth.currentUser?.uid ?: error("Sign in is required")
        val snapshot = profileCollection(uid).document(profileId).get().await()
        require(snapshot.exists()) { "Cloud profile was not found" }
        val schemaVersion = snapshot.getLong("schemaVersion")?.toInt() ?: 0
        require(schemaVersion in 1..SCHEMA_VERSION) { "This profile was created by a newer app version" }
        val portable = (snapshot.get("portableSettings") as? Map<*, *>)?.toJsonObject()
            ?: error("Cloud profile has no emulator settings")
        val perGame = (snapshot.get("perGameSettings") as? Map<*, *>)?.toJsonObject()
            ?: JSONObject().put("profiles", JSONArray())

        preferences.importEmulatorCloudJson(portable)
        perGameSettings.importJson(sanitizePerGameSettings(perGame))
    }

    suspend fun delete(profileId: String) {
        val uid = auth.currentUser?.uid ?: return
        profileCollection(uid).document(profileId).delete().await()
    }

    private fun profileCollection(uid: String) = firestore.collection(USERS)
        .document(uid)
        .collection(EMULATOR_PROFILES)

    /** Never move a private local GPU driver path into or out of the cloud. */
    private fun sanitizePerGameSettings(root: JSONObject): JSONObject {
        val clean = JSONObject(root.toString())
        val profiles = clean.optJSONArray("profiles") ?: return clean
        for (index in 0 until profiles.length()) {
            val item = profiles.optJSONObject(index) ?: continue
            item.remove("customDriverPath")
            item.put("gpuDriverType", 0)
        }
        return clean
    }

    private fun JSONObject.toFirestoreValue(): Map<String, Any?> = buildMap {
        keys().forEach { key -> put(key, opt(key).toFirestoreValue()) }
    }

    private fun Any?.toFirestoreValue(): Any? = when (this) {
        null, JSONObject.NULL -> null
        is JSONObject -> toFirestoreValue()
        is JSONArray -> (0 until length()).map { opt(it).toFirestoreValue() }
        is String, is Boolean, is Int, is Long, is Float, is Double -> this
        is Number -> toDouble()
        else -> toString()
    }

    private fun Map<*, *>.toJsonObject(): JSONObject = JSONObject().apply {
        forEach { (key, value) -> if (key is String) put(key, value.toJsonValue()) }
    }

    private fun Any?.toJsonValue(): Any = when (this) {
        null -> JSONObject.NULL
        is Map<*, *> -> toJsonObject()
        is List<*> -> JSONArray().apply { this@toJsonValue.forEach { put(it.toJsonValue()) } }
        else -> this
    }

    private fun DocumentSnapshot.toCloudProfile(): CloudEmulatorProfile? {
        if (!exists()) return null
        return CloudEmulatorProfile(
            id = getString("profileId") ?: id,
            name = getString("name").orEmpty(),
            sourceDeviceId = getString("sourceDeviceId").orEmpty(),
            appVersion = getString("appVersion").orEmpty(),
            coreVersion = getString("coreVersion").orEmpty(),
            updatedAtMs = getTimestamp(FIELD_UPDATED_AT)?.toDate()?.time
        )
    }

    private suspend fun <T> Task<T>.await(): T = suspendCancellableCoroutine { continuation ->
        addOnSuccessListener { continuation.resume(it) }
        addOnFailureListener { continuation.resumeWithException(it) }
        addOnCanceledListener { continuation.cancel() }
    }

    private companion object {
        const val USERS = "users"
        const val EMULATOR_PROFILES = "emulatorProfiles"
        const val FIELD_CREATED_AT = "createdAt"
        const val FIELD_UPDATED_AT = "updatedAt"
        const val SCHEMA_VERSION = 1
        const val MAX_PROFILES = 5
        const val MAX_NAME_LENGTH = 64
        const val MAX_PAYLOAD_BYTES = 750_000
    }
}
