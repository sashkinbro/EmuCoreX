// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: GPL-3.0+

package com.sbro.emucorex.network

import com.sbro.emucorex.BuildConfig
import java.net.HttpURLConnection
import java.net.URL
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import org.webrtc.IceCandidate
import org.webrtc.PeerConnection
import org.webrtc.SessionDescription

internal enum class MultiplayerMode(val apiName: String) {
    InternetLink("internet_link"),
    NetPlay("netplay"),
    RemotePlay("remote_play")
}

@Serializable
internal data class SignalDescription(val type: String, val sdp: String) {
    fun toWebRtc(): SessionDescription = SessionDescription(
        SessionDescription.Type.fromCanonicalForm(type),
        sdp
    )
}

@Serializable
internal data class SignalCandidate(
    val id: Int = 0,
    val role: String = "",
    val mid: String,
    val line: Int,
    val candidate: String
) {
    fun toWebRtc(): IceCandidate = IceCandidate(mid, line, candidate)
}

@Serializable
internal data class SignalIceServer(
    val urls: List<String>,
    val username: String = "",
    val credential: String = ""
) {
    fun toWebRtc(): PeerConnection.IceServer {
        val builder = PeerConnection.IceServer.builder(urls)
        if (username.isNotBlank()) builder.setUsername(username)
        if (credential.isNotBlank()) builder.setPassword(credential)
        return builder.createIceServer()
    }
}

@Serializable
internal data class CreatedSignalRoom(
    val code: String,
    val token: String,
    val iceServers: List<SignalIceServer>,
    val expiresAt: Long
)

@Serializable
internal data class JoinedSignalRoom(
    val token: String,
    val offer: SignalDescription,
    val iceServers: List<SignalIceServer>,
    val expiresAt: Long
)

@Serializable
internal data class SignalState(
    val answer: SignalDescription? = null,
    val candidates: List<SignalCandidate> = emptyList(),
    val expiresAt: Long
)

@Serializable
private data class ModeRequest(val mode: String)

@Serializable
private data class ApiError(val error: String = "internal_error")

internal class SignalingException(val status: Int, val code: String) : Exception(code)

internal object MultiplayerSignalingClient {
    private const val CONNECT_TIMEOUT_MS = 12_000
    private const val READ_TIMEOUT_MS = 20_000
    private val json = Json { ignoreUnknownKeys = true }
    private val baseUrl: String get() = BuildConfig.MULTIPLAYER_SIGNALING_URL.trimEnd('/')

    fun createRoom(mode: MultiplayerMode): CreatedSignalRoom = request(
        method = "POST",
        path = "/v1/rooms",
        body = json.encodeToString(ModeRequest(mode.apiName))
    )

    fun joinRoom(mode: MultiplayerMode, code: String): JoinedSignalRoom = request(
        method = "POST",
        path = "/v1/rooms/$code/join",
        body = json.encodeToString(ModeRequest(mode.apiName))
    )

    fun publishOffer(code: String, token: String, description: SessionDescription) {
        request<UnitResponse>("PUT", "/v1/rooms/$code/offer", token, description.toJson())
    }

    fun publishAnswer(code: String, token: String, description: SessionDescription) {
        request<UnitResponse>("PUT", "/v1/rooms/$code/answer", token, description.toJson())
    }

    fun publishCandidate(code: String, token: String, candidate: IceCandidate) {
        request<CandidateId>(
            "POST",
            "/v1/rooms/$code/candidates",
            token,
            json.encodeToString(
                SignalCandidate(
                    mid = candidate.sdpMid.orEmpty(),
                    line = candidate.sdpMLineIndex,
                    candidate = candidate.sdp
                )
            )
        )
    }

    fun state(code: String, token: String, after: Int): SignalState =
        request("GET", "/v1/rooms/$code/state?after=$after", token)

    fun deleteRoom(code: String, token: String) {
        request<UnitResponse>("DELETE", "/v1/rooms/$code", token)
    }

    private inline fun <reified T> request(
        method: String,
        path: String,
        token: String = "",
        body: String? = null
    ): T {
        check(baseUrl.startsWith("https://")) { "Multiplayer signaling endpoint is not configured" }
        val connection = URL(baseUrl + path).openConnection() as HttpURLConnection
        try {
            connection.requestMethod = method
            connection.connectTimeout = CONNECT_TIMEOUT_MS
            connection.readTimeout = READ_TIMEOUT_MS
            connection.setRequestProperty("Accept", "application/json")
            connection.setRequestProperty("Cache-Control", "no-store")
            connection.setRequestProperty("X-EmuCoreX-Client", BuildConfig.MULTIPLAYER_CLIENT_CODE)
            if (token.isNotBlank()) connection.setRequestProperty("Authorization", "Bearer $token")
            if (body != null) {
                connection.doOutput = true
                connection.setRequestProperty("Content-Type", "application/json; charset=utf-8")
                connection.outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }
            }
            val status = connection.responseCode
            val responseBody = (if (status in 200..299) connection.inputStream else connection.errorStream)
                ?.bufferedReader(Charsets.UTF_8)?.use { it.readText() }.orEmpty()
            if (status !in 200..299) {
                val error = runCatching { json.decodeFromString<ApiError>(responseBody).error }
                    .getOrDefault("http_$status")
                throw SignalingException(status, error)
            }
            return json.decodeFromString(responseBody)
        } finally {
            connection.disconnect()
        }
    }
}

@Serializable
private data class UnitResponse(val ok: Boolean = false)

@Serializable
private data class CandidateId(val id: Int)

private fun SessionDescription.toJson(): String = Json.encodeToString(
    SignalDescription(type.canonicalForm(), description)
)
