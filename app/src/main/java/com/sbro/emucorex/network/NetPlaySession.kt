// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: GPL-3.0+

package com.sbro.emucorex.network

import android.content.Context
import com.sbro.emucorex.core.NativeApp
import java.nio.ByteBuffer
import java.nio.ByteOrder
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
import org.webrtc.MediaStream
import org.webrtc.PeerConnection
import org.webrtc.PeerConnectionFactory
import org.webrtc.RtpReceiver
import org.webrtc.SdpObserver
import org.webrtc.SessionDescription
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

enum class NetPlayStatus { Idle, Creating, WaitingForPeer, Joining, Connecting, Connected, Error }

data class NetPlayState(
    val status: NetPlayStatus = NetPlayStatus.Idle,
    val roomCode: String = "",
    val isHost: Boolean = false,
    val error: InternetLinkError? = null
)

object NetPlaySession {
    private const val CHANNEL_LABEL = "emucorex-netplay-input"
    private const val INPUT_PACKET_SIZE = 13
    private const val POLL_INTERVAL_MS = 650L

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val _state = MutableStateFlow(NetPlayState())
    val state: StateFlow<NetPlayState> = _state.asStateFlow()

    private var factory: PeerConnectionFactory? = null
    private var peer: PeerConnection? = null
    private var inputChannel: DataChannel? = null
    private var signalingPoll: Job? = null
    private var roomCode = ""
    private var roomToken = ""
    private var lastCandidateId = 0
    private var remoteDescriptionApplied = false
    private var closing = false
    private val pendingRemoteCandidates = mutableListOf<IceCandidate>()
    private val pendingLocalCandidates = mutableListOf<IceCandidate>()

    @Synchronized
    fun initialize(context: Context) {
        if (factory != null) return
        PeerConnectionFactory.initialize(
            PeerConnectionFactory.InitializationOptions.builder(context.applicationContext)
                .createInitializationOptions()
        )
        factory = PeerConnectionFactory.builder().createPeerConnectionFactory()
    }

    fun createRoom(context: Context) {
        initialize(context)
        scope.launch {
            closeSession(deleteRoom = true)
            _state.value = NetPlayState(NetPlayStatus.Creating, isHost = true)
            try {
                val created = MultiplayerSignalingClient.createRoom(MultiplayerMode.NetPlay)
                roomCode = created.code
                roomToken = created.token
                _state.value = NetPlayState(NetPlayStatus.Creating, roomCode, true)
                val connection = createPeer(true, created.iceServers)
                bindInputChannel(connection.createDataChannel(CHANNEL_LABEL, DataChannel.Init()))
                val offer = connection.createNetPlayOfferAwait()
                connection.setNetPlayLocalDescriptionAwait(offer)
                MultiplayerSignalingClient.publishOffer(roomCode, roomToken, offer)
                flushLocalCandidates()
                startSignalingPoll(isHost = true)
                _state.value = NetPlayState(NetPlayStatus.WaitingForPeer, roomCode, true)
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
            _state.value = NetPlayState(NetPlayStatus.Joining, code, false)
            try {
                val joined = MultiplayerSignalingClient.joinRoom(MultiplayerMode.NetPlay, code)
                roomToken = joined.token
                val connection = createPeer(false, joined.iceServers)
                connection.setNetPlayRemoteDescriptionAwait(joined.offer.toWebRtc())
                remoteDescriptionApplied = true
                flushRemoteCandidates()
                val answer = connection.createNetPlayAnswerAwait()
                connection.setNetPlayLocalDescriptionAwait(answer)
                MultiplayerSignalingClient.publishAnswer(code, roomToken, answer)
                flushLocalCandidates()
                startSignalingPoll(isHost = false)
                _state.value = NetPlayState(NetPlayStatus.Connecting, code, false)
            } catch (error: Throwable) {
                if (error !is CancellationException) fail(error.toInternetLinkError())
            }
        }
    }

    /** Returns the controller port to use. The guest always controls player 2. */
    fun mapAndSendLocalButton(padIndex: Int, buttonIndex: Int, range: Int, pressed: Boolean): Int {
        val current = _state.value
        if (current.status != NetPlayStatus.Connected) return padIndex
        val mappedPad = if (current.isHost) 0 else 1
        val channel = inputChannel
        if (channel?.state() == DataChannel.State.OPEN) {
            val packet = ByteBuffer.allocate(INPUT_PACKET_SIZE).order(ByteOrder.LITTLE_ENDIAN)
                .put(1)
                .putInt(mappedPad)
                .putInt(buttonIndex)
                .putInt(encodeNetworkButtonRange(range, pressed))
                .apply { flip() }
            channel.send(DataChannel.Buffer(packet, true))
        }
        return mappedPad
    }

    fun sanitizeRoomCode(value: String): String = sanitizeMultiplayerRoomCode(value)

    fun disconnect() {
        scope.launch { closeSession(deleteRoom = true); _state.value = NetPlayState() }
    }

    private fun createPeer(isHost: Boolean, servers: List<SignalIceServer>): PeerConnection {
        val config = PeerConnection.RTCConfiguration(servers.map(SignalIceServer::toWebRtc)).apply {
            sdpSemantics = PeerConnection.SdpSemantics.UNIFIED_PLAN
            continualGatheringPolicy = PeerConnection.ContinualGatheringPolicy.GATHER_CONTINUALLY
        }
        val connection = factory?.createPeerConnection(config, object : NetPlayPeerObserver() {
            override fun onIceCandidate(candidate: IceCandidate) = publishCandidate(candidate)
            override fun onDataChannel(channel: DataChannel) {
                if (!isHost && channel.label() == CHANNEL_LABEL) bindInputChannel(channel)
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
        }) ?: error("Unable to create NetPlay peer")
        peer = connection
        return connection
    }

    private fun startSignalingPoll(isHost: Boolean) {
        signalingPoll?.cancel()
        signalingPoll = scope.launch {
            while (isActive) {
                try {
                    val state = MultiplayerSignalingClient.state(roomCode, roomToken, lastCandidateId)
                    if (isHost && !remoteDescriptionApplied && state.answer != null) {
                        peer?.setNetPlayRemoteDescriptionAwait(state.answer.toWebRtc())
                        remoteDescriptionApplied = true
                        flushRemoteCandidates()
                        _state.value = _state.value.copy(status = NetPlayStatus.Connecting)
                    }
                    state.candidates.forEach { candidate ->
                        lastCandidateId = maxOf(lastCandidateId, candidate.id)
                        val value = candidate.toWebRtc()
                        if (remoteDescriptionApplied) peer?.addIceCandidate(value)
                        else synchronized(pendingRemoteCandidates) { pendingRemoteCandidates += value }
                    }
                } catch (error: SignalingException) {
                    if (error.status == 404 || error.status == 403) {
                        fail(InternetLinkError.ConnectionFailed)
                        return@launch
                    }
                } catch (_: Throwable) {
                    // Retry transient failures without tearing down the active WebRTC session.
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

    private fun bindInputChannel(channel: DataChannel?) {
        channel ?: return
        inputChannel = channel
        channel.registerObserver(object : DataChannel.Observer {
            override fun onBufferedAmountChange(previousAmount: Long) = Unit
            override fun onStateChange() { if (channel.state() == DataChannel.State.OPEN) markConnected() }
            override fun onMessage(buffer: DataChannel.Buffer) {
                if (!buffer.binary || buffer.data.remaining() != INPUT_PACKET_SIZE) return
                val packet = buffer.data.order(ByteOrder.LITTLE_ENDIAN)
                if (packet.get().toInt() != 1) return
                val pad = packet.int.coerceIn(0, 1)
                val button = packet.int
                val range = packet.int.coerceIn(0, 255)
                NativeApp.setPadButton(pad, button, range, range > 0)
            }
        })
    }

    private fun markConnected() {
        _state.value = _state.value.copy(status = NetPlayStatus.Connected, error = null)
    }

    private fun flushRemoteCandidates() {
        val copy = synchronized(pendingRemoteCandidates) {
            pendingRemoteCandidates.toList().also { pendingRemoteCandidates.clear() }
        }
        copy.forEach { peer?.addIceCandidate(it) }
    }

    private suspend fun closeSession(deleteRoom: Boolean) {
        closing = true
        val code = roomCode
        val token = roomToken
        val wasHost = _state.value.isHost
        signalingPoll?.cancel(); signalingPoll = null
        inputChannel?.unregisterObserver(); inputChannel?.close(); inputChannel = null
        peer?.close(); peer = null
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
        _state.value = _state.value.copy(status = NetPlayStatus.Error, error = error)
    }
}

internal fun encodeNetworkButtonRange(range: Int, pressed: Boolean): Int {
    if (!pressed) return 0
    return range.coerceIn(0, 255).takeIf { it > 0 } ?: 255
}

private abstract class NetPlayPeerObserver : PeerConnection.Observer {
    override fun onSignalingChange(state: PeerConnection.SignalingState) = Unit
    override fun onIceConnectionChange(state: PeerConnection.IceConnectionState) = Unit
    override fun onIceConnectionReceivingChange(receiving: Boolean) = Unit
    override fun onIceGatheringChange(state: PeerConnection.IceGatheringState) = Unit
    override fun onIceCandidatesRemoved(candidates: Array<out IceCandidate>) = Unit
    override fun onAddStream(stream: MediaStream) = Unit
    override fun onRemoveStream(stream: MediaStream) = Unit
    override fun onRenegotiationNeeded() = Unit
    override fun onAddTrack(receiver: RtpReceiver, streams: Array<out MediaStream>) = Unit
}

private suspend fun PeerConnection.createNetPlayOfferAwait(): SessionDescription =
    suspendCancellableCoroutine { createOffer(NetPlayCreateSdpContinuation(it), MediaConstraints()) }
private suspend fun PeerConnection.createNetPlayAnswerAwait(): SessionDescription =
    suspendCancellableCoroutine { createAnswer(NetPlayCreateSdpContinuation(it), MediaConstraints()) }
private suspend fun PeerConnection.setNetPlayLocalDescriptionAwait(value: SessionDescription) =
    suspendCancellableCoroutine<Unit> { setLocalDescription(NetPlaySetSdpContinuation(it), value) }
private suspend fun PeerConnection.setNetPlayRemoteDescriptionAwait(value: SessionDescription) =
    suspendCancellableCoroutine<Unit> { setRemoteDescription(NetPlaySetSdpContinuation(it), value) }

private class NetPlayCreateSdpContinuation(
    private val continuation: kotlinx.coroutines.CancellableContinuation<SessionDescription>
) : SdpObserver {
    override fun onCreateSuccess(value: SessionDescription) = continuation.resume(value)
    override fun onCreateFailure(error: String) = continuation.resumeWithException(IllegalStateException(error))
    override fun onSetSuccess() = Unit
    override fun onSetFailure(error: String) = Unit
}

private class NetPlaySetSdpContinuation(
    private val continuation: kotlinx.coroutines.CancellableContinuation<Unit>
) : SdpObserver {
    override fun onCreateSuccess(value: SessionDescription) = Unit
    override fun onCreateFailure(error: String) = Unit
    override fun onSetSuccess() = continuation.resume(Unit)
    override fun onSetFailure(error: String) = continuation.resumeWithException(IllegalStateException(error))
}
