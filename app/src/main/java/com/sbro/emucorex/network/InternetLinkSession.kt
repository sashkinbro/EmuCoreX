// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: GPL-3.0+

package com.sbro.emucorex.network

import android.content.Context
import com.sbro.emucorex.core.NativeApp
import java.nio.ByteBuffer
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import org.webrtc.DataChannel
import org.webrtc.IceCandidate
import org.webrtc.MediaConstraints
import org.webrtc.PeerConnection
import org.webrtc.PeerConnectionFactory
import org.webrtc.SdpObserver
import org.webrtc.SessionDescription
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

enum class InternetLinkStatus { Idle, Creating, WaitingForPeer, Joining, Connecting, Connected, Error }

data class InternetLinkState(
    val status: InternetLinkStatus = InternetLinkStatus.Idle,
    val roomCode: String = "",
    val isHost: Boolean = false,
    val error: InternetLinkError? = null
)

enum class InternetLinkError { InvalidRoomCode, RoomNotFound, RoomFull, ConnectionFailed }

object InternetLinkSession {
    private const val FRAME_LABEL = "emucorex-dev9"
    private const val MAX_FRAME_SIZE = 1514
    private const val POLL_INTERVAL_MS = 650L

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val _state = MutableStateFlow(InternetLinkState())
    val state: StateFlow<InternetLinkState> = _state.asStateFlow()

    private var factory: PeerConnectionFactory? = null
    private var peerConnection: PeerConnection? = null
    private var dataChannel: DataChannel? = null
    private var framePump: Job? = null
    private var signalingPoll: Job? = null
    private var roomToken = ""
    private var roomCode = ""
    private var remoteDescriptionApplied = false
    private var closing = false
    private var lastCandidateId = 0
    private val pendingRemoteCandidates = mutableListOf<IceCandidate>()
    private val pendingLocalCandidates = mutableListOf<IceCandidate>()

    @Synchronized
    fun initialize(context: Context) {
        if (factory != null) return
        PeerConnectionFactory.initialize(
            PeerConnectionFactory.InitializationOptions.builder(context.applicationContext)
                .setEnableInternalTracer(false)
                .createInitializationOptions()
        )
        factory = PeerConnectionFactory.builder().createPeerConnectionFactory()
    }

    fun createRoom(context: Context) {
        initialize(context)
        scope.launch {
            closeSession(deleteRoom = true)
            _state.value = InternetLinkState(InternetLinkStatus.Creating, isHost = true)
            try {
                val created = MultiplayerSignalingClient.createRoom(MultiplayerMode.InternetLink)
                roomCode = created.code
                roomToken = created.token
                _state.value = InternetLinkState(InternetLinkStatus.Creating, roomCode, true)
                val peer = createPeer(true, created.iceServers)
                bindDataChannel(
                    peer.createDataChannel(
                        FRAME_LABEL,
                        DataChannel.Init().apply { ordered = false; maxRetransmits = 0 }
                    ) ?: error("Unable to create DEV9 data channel")
                )
                val offer = peer.createOfferAwait()
                peer.setLocalDescriptionAwait(offer)
                MultiplayerSignalingClient.publishOffer(roomCode, roomToken, offer)
                flushLocalCandidates()
                startSignalingPoll(isHost = true)
                _state.value = InternetLinkState(InternetLinkStatus.WaitingForPeer, roomCode, true)
            } catch (error: Throwable) {
                if (error !is CancellationException) fail(error.toInternetLinkError())
            }
        }
    }

    fun joinRoom(context: Context, rawCode: String) {
        initialize(context)
        val code = sanitizeRoomCode(rawCode)
        if (code.length != MULTIPLAYER_ROOM_CODE_LENGTH) {
            fail(InternetLinkError.InvalidRoomCode)
            return
        }
        scope.launch {
            closeSession(deleteRoom = true)
            roomCode = code
            _state.value = InternetLinkState(InternetLinkStatus.Joining, code, false)
            try {
                val joined = MultiplayerSignalingClient.joinRoom(MultiplayerMode.InternetLink, code)
                roomToken = joined.token
                val peer = createPeer(false, joined.iceServers)
                peer.setRemoteDescriptionAwait(joined.offer.toWebRtc())
                remoteDescriptionApplied = true
                flushRemoteCandidates()
                val answer = peer.createAnswerAwait()
                peer.setLocalDescriptionAwait(answer)
                MultiplayerSignalingClient.publishAnswer(code, roomToken, answer)
                flushLocalCandidates()
                startSignalingPoll(isHost = false)
                _state.value = InternetLinkState(InternetLinkStatus.Connecting, code, false)
            } catch (error: Throwable) {
                if (error !is CancellationException) fail(error.toInternetLinkError())
            }
        }
    }

    fun disconnect() {
        scope.launch { closeSession(deleteRoom = true); _state.value = InternetLinkState() }
    }

    private fun createPeer(isHost: Boolean, servers: List<SignalIceServer>): PeerConnection {
        val configuration = PeerConnection.RTCConfiguration(servers.map(SignalIceServer::toWebRtc)).apply {
            sdpSemantics = PeerConnection.SdpSemantics.UNIFIED_PLAN
            continualGatheringPolicy = PeerConnection.ContinualGatheringPolicy.GATHER_CONTINUALLY
        }
        val peer = factory?.createPeerConnection(configuration, object : PeerObserver() {
            override fun onIceCandidate(candidate: IceCandidate) = publishCandidate(candidate)
            override fun onDataChannel(channel: DataChannel) {
                if (!isHost && channel.label() == FRAME_LABEL) bindDataChannel(channel)
            }
            override fun onConnectionChange(newState: PeerConnection.PeerConnectionState) {
                if (closing) return
                when (newState) {
                    PeerConnection.PeerConnectionState.CONNECTED -> markConnected()
                    PeerConnection.PeerConnectionState.FAILED,
                    PeerConnection.PeerConnectionState.CLOSED -> fail(InternetLinkError.ConnectionFailed)
                    else -> Unit
                }
            }
        }) ?: error("Unable to create peer connection")
        peerConnection = peer
        return peer
    }

    private fun startSignalingPoll(isHost: Boolean) {
        signalingPoll?.cancel()
        signalingPoll = scope.launch {
            while (isActive) {
                try {
                    val state = MultiplayerSignalingClient.state(roomCode, roomToken, lastCandidateId)
                    if (isHost && !remoteDescriptionApplied && state.answer != null) {
                        peerConnection?.setRemoteDescriptionAwait(state.answer.toWebRtc())
                        remoteDescriptionApplied = true
                        flushRemoteCandidates()
                        _state.value = _state.value.copy(status = InternetLinkStatus.Connecting)
                    }
                    state.candidates.forEach { candidate ->
                        lastCandidateId = maxOf(lastCandidateId, candidate.id)
                        val value = candidate.toWebRtc()
                        if (remoteDescriptionApplied) peerConnection?.addIceCandidate(value)
                        else synchronized(pendingRemoteCandidates) { pendingRemoteCandidates += value }
                    }
                } catch (error: SignalingException) {
                    if (error.status == 404 || error.status == 403) {
                        fail(InternetLinkError.ConnectionFailed)
                        return@launch
                    }
                } catch (_: Throwable) {
                    // Transient network failures are retried while WebRTC is connecting.
                }
                delay(POLL_INTERVAL_MS)
            }
        }
    }

    private fun publishCandidate(candidate: IceCandidate) {
        val code = roomCode
        val token = roomToken
        if (code.isBlank() || token.isBlank()) {
            synchronized(pendingLocalCandidates) { pendingLocalCandidates += candidate }
            return
        }
        scope.launch { publishCandidateWithRetry(code, token, candidate) }
    }

    private suspend fun publishCandidateWithRetry(code: String, token: String, candidate: IceCandidate) {
        repeat(3) { attempt ->
            if (runCatching { MultiplayerSignalingClient.publishCandidate(code, token, candidate) }.isSuccess) return
            delay(250L * (attempt + 1))
        }
    }

    private suspend fun flushLocalCandidates() {
        val copy = synchronized(pendingLocalCandidates) {
            pendingLocalCandidates.toList().also { pendingLocalCandidates.clear() }
        }
        copy.forEach { publishCandidateWithRetry(roomCode, roomToken, it) }
    }

    private fun bindDataChannel(channel: DataChannel) {
        dataChannel?.unregisterObserver()
        dataChannel = channel
        channel.registerObserver(object : DataChannel.Observer {
            override fun onBufferedAmountChange(previousAmount: Long) = Unit
            override fun onStateChange() { if (channel.state() == DataChannel.State.OPEN) markConnected() }
            override fun onMessage(buffer: DataChannel.Buffer) {
                if (!buffer.binary) return
                val bytes = ByteArray(buffer.data.remaining())
                buffer.data.get(bytes)
                if (bytes.size in 1..MAX_FRAME_SIZE) NativeApp.pushInternetLinkFrame(bytes)
            }
        })
    }

    private fun markConnected() {
        val current = _state.value
        if (current.status == InternetLinkStatus.Connected) return
        NativeApp.setInternetLinkTransportReady(true)
        _state.value = current.copy(status = InternetLinkStatus.Connected, error = null)
        framePump?.cancel()
        framePump = scope.launch {
            while (isActive) {
                val channel = dataChannel
                var sent = false
                if (channel?.state() == DataChannel.State.OPEN) {
                    for (index in 0 until 32) {
                        val frame = NativeApp.pollInternetLinkFrame() ?: break
                        sent = channel.send(DataChannel.Buffer(ByteBuffer.wrap(frame), true)) || sent
                    }
                }
                if (!sent) delay(2)
            }
        }
    }

    private suspend fun closeSession(deleteRoom: Boolean) {
        closing = true
        val code = roomCode
        val token = roomToken
        val wasHost = _state.value.isHost
        signalingPoll?.cancel(); signalingPoll = null
        framePump?.cancel(); framePump = null
        dataChannel?.unregisterObserver(); dataChannel?.close(); dataChannel = null
        peerConnection?.close(); peerConnection = null
        NativeApp.resetInternetLinkTransport()
        if (deleteRoom && wasHost && code.isNotBlank() && token.isNotBlank()) {
            runCatching { MultiplayerSignalingClient.deleteRoom(code, token) }
        }
        roomCode = ""; roomToken = ""; lastCandidateId = 0
        remoteDescriptionApplied = false
        synchronized(pendingRemoteCandidates) { pendingRemoteCandidates.clear() }
        synchronized(pendingLocalCandidates) { pendingLocalCandidates.clear() }
        closing = false
    }

    private fun fail(error: InternetLinkError) {
        NativeApp.setInternetLinkTransportReady(false)
        _state.value = _state.value.copy(status = InternetLinkStatus.Error, error = error)
    }

    private fun flushRemoteCandidates() {
        val copy = synchronized(pendingRemoteCandidates) {
            pendingRemoteCandidates.toList().also { pendingRemoteCandidates.clear() }
        }
        copy.forEach { peerConnection?.addIceCandidate(it) }
    }

    fun sanitizeRoomCode(value: String): String = sanitizeMultiplayerRoomCode(value)
}

internal fun Throwable.toInternetLinkError(): InternetLinkError = when (this) {
    is SignalingException -> when {
        status == 404 -> InternetLinkError.RoomNotFound
        status == 409 && code == "room_full" -> InternetLinkError.RoomFull
        else -> InternetLinkError.ConnectionFailed
    }
    else -> InternetLinkError.ConnectionFailed
}

private abstract class PeerObserver : PeerConnection.Observer {
    override fun onSignalingChange(state: PeerConnection.SignalingState) = Unit
    override fun onIceConnectionChange(state: PeerConnection.IceConnectionState) = Unit
    override fun onIceConnectionReceivingChange(receiving: Boolean) = Unit
    override fun onIceGatheringChange(state: PeerConnection.IceGatheringState) = Unit
    override fun onIceCandidatesRemoved(candidates: Array<out IceCandidate>) = Unit
    override fun onAddStream(stream: org.webrtc.MediaStream) = Unit
    override fun onRemoveStream(stream: org.webrtc.MediaStream) = Unit
    override fun onRenegotiationNeeded() = Unit
    override fun onAddTrack(receiver: org.webrtc.RtpReceiver, streams: Array<out org.webrtc.MediaStream>) = Unit
}

private suspend fun PeerConnection.createOfferAwait(): SessionDescription =
    suspendCancellableCoroutine { createOffer(SdpContinuation(it), MediaConstraints()) }
private suspend fun PeerConnection.createAnswerAwait(): SessionDescription =
    suspendCancellableCoroutine { createAnswer(SdpContinuation(it), MediaConstraints()) }
private suspend fun PeerConnection.setLocalDescriptionAwait(value: SessionDescription) =
    suspendCancellableCoroutine<Unit> { setLocalDescription(SetSdpContinuation(it), value) }
private suspend fun PeerConnection.setRemoteDescriptionAwait(value: SessionDescription) =
    suspendCancellableCoroutine<Unit> { setRemoteDescription(SetSdpContinuation(it), value) }

private class SdpContinuation(
    private val continuation: kotlinx.coroutines.CancellableContinuation<SessionDescription>
) : SdpObserver {
    override fun onCreateSuccess(description: SessionDescription) = continuation.resume(description)
    override fun onCreateFailure(message: String) = continuation.resumeWithException(IllegalStateException(message))
    override fun onSetSuccess() = Unit
    override fun onSetFailure(message: String) = Unit
}

private class SetSdpContinuation(
    private val continuation: kotlinx.coroutines.CancellableContinuation<Unit>
) : SdpObserver {
    override fun onCreateSuccess(description: SessionDescription) = Unit
    override fun onCreateFailure(message: String) = Unit
    override fun onSetSuccess() = continuation.resume(Unit)
    override fun onSetFailure(message: String) = continuation.resumeWithException(IllegalStateException(message))
}
