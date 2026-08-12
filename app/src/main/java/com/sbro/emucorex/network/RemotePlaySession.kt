// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: GPL-3.0+

package com.sbro.emucorex.network

import android.content.Context
import android.content.Intent
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
import org.webrtc.EglBase
import org.webrtc.IceCandidate
import org.webrtc.MediaConstraints
import org.webrtc.MediaStream
import org.webrtc.PeerConnection
import org.webrtc.PeerConnectionFactory
import org.webrtc.RtpReceiver
import org.webrtc.ScreenCapturerAndroid
import org.webrtc.SdpObserver
import org.webrtc.SessionDescription
import org.webrtc.SurfaceTextureHelper
import org.webrtc.VideoCapturer
import org.webrtc.VideoTrack
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

enum class RemotePlayStatus { Idle, Creating, WaitingForPeer, Joining, Connecting, Connected, Error }

data class RemotePlayState(
    val status: RemotePlayStatus = RemotePlayStatus.Idle,
    val roomCode: String = "",
    val isHost: Boolean = false,
    val error: InternetLinkError? = null
)

object RemotePlaySession {
    private const val CONTROL_LABEL = "emucorex-control"
    private const val VIDEO_WIDTH = 1280
    private const val VIDEO_HEIGHT = 720
    private const val VIDEO_FPS = 60
    private const val POLL_INTERVAL_MS = 650L

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val _state = MutableStateFlow(RemotePlayState())
    val state: StateFlow<RemotePlayState> = _state.asStateFlow()
    private val _remoteVideoTrack = MutableStateFlow<VideoTrack?>(null)
    val remoteVideoTrack: StateFlow<VideoTrack?> = _remoteVideoTrack.asStateFlow()
    val eglContext: EglBase.Context get() = requireNotNull(eglBase).eglBaseContext

    private var eglBase: EglBase? = null
    private var factory: PeerConnectionFactory? = null
    private var peer: PeerConnection? = null
    private var controlChannel: DataChannel? = null
    private var remoteTrack: VideoTrack? = null
    private var capturer: VideoCapturer? = null
    private var textureHelper: SurfaceTextureHelper? = null
    private var applicationContext: Context? = null
    private var signalingPoll: Job? = null
    private var roomCode = ""
    private var roomToken = ""
    private var lastCandidateId = 0
    private var remoteDescriptionApplied = false
    private var closing = false
    private val pendingCandidates = mutableListOf<IceCandidate>()
    private val pendingLocalCandidates = mutableListOf<IceCandidate>()

    @Synchronized
    fun initialize(context: Context) {
        if (factory != null) return
        applicationContext = context.applicationContext
        eglBase = EglBase.create()
        PeerConnectionFactory.initialize(
            PeerConnectionFactory.InitializationOptions.builder(context.applicationContext).createInitializationOptions()
        )
        factory = PeerConnectionFactory.builder()
            .setVideoEncoderFactory(org.webrtc.DefaultVideoEncoderFactory(eglContext, true, true))
            .setVideoDecoderFactory(org.webrtc.DefaultVideoDecoderFactory(eglContext))
            .createPeerConnectionFactory()
    }

    fun host(context: Context, capturePermission: Intent) {
        initialize(context)
        scope.launch {
            closeSession(deleteRoom = true)
            _state.value = RemotePlayState(RemotePlayStatus.Creating, isHost = true)
            try {
                val created = MultiplayerSignalingClient.createRoom(MultiplayerMode.RemotePlay)
                roomCode = created.code
                roomToken = created.token
                _state.value = RemotePlayState(RemotePlayStatus.Creating, roomCode, true)
                val connection = createPeer(true, created.iceServers)
                bindControlChannel(connection.createDataChannel(CONTROL_LABEL, DataChannel.Init()))
                startScreenCapture(context, connection, capturePermission)
                val offer = connection.createRemoteOfferAwait()
                connection.setRemoteLocalDescriptionAwait(offer)
                MultiplayerSignalingClient.publishOffer(roomCode, roomToken, offer)
                flushLocalCandidates()
                startSignalingPoll(isHost = true)
                _state.value = RemotePlayState(RemotePlayStatus.WaitingForPeer, roomCode, true)
            } catch (error: Throwable) {
                if (error !is CancellationException) fail(error.toInternetLinkError())
            }
        }
    }

    fun join(context: Context, rawCode: String) {
        initialize(context)
        val code = sanitizeCode(rawCode)
        if (code.length != MULTIPLAYER_ROOM_CODE_LENGTH) {
            fail(InternetLinkError.InvalidRoomCode)
            return
        }
        scope.launch {
            closeSession(deleteRoom = true)
            roomCode = code
            _state.value = RemotePlayState(RemotePlayStatus.Joining, code, false)
            try {
                val joined = MultiplayerSignalingClient.joinRoom(MultiplayerMode.RemotePlay, code)
                roomToken = joined.token
                val connection = createPeer(false, joined.iceServers)
                connection.setRemoteRemoteDescriptionAwait(joined.offer.toWebRtc())
                remoteDescriptionApplied = true
                flushCandidates()
                val answer = connection.createRemoteAnswerAwait()
                connection.setRemoteLocalDescriptionAwait(answer)
                MultiplayerSignalingClient.publishAnswer(code, roomToken, answer)
                flushLocalCandidates()
                startSignalingPoll(isHost = false)
                _state.value = RemotePlayState(RemotePlayStatus.Connecting, code, false)
            } catch (error: Throwable) {
                if (error !is CancellationException) fail(error.toInternetLinkError())
            }
        }
    }

    fun sendButton(index: Int, range: Int, pressed: Boolean) {
        sendControllerButton(padIndex = 1, index = index, range = range, pressed = pressed)
    }

    /** Forwards a guest's physical or mapped controller input to player 2 on the host. */
    fun forwardGuestButton(index: Int, range: Int, pressed: Boolean): Boolean {
        val current = _state.value
        if (current.status != RemotePlayStatus.Connected || current.isHost) return false
        sendControllerButton(padIndex = 1, index = index, range = range, pressed = pressed)
        return true
    }

    private fun sendControllerButton(padIndex: Int, index: Int, range: Int, pressed: Boolean) {
        val channel = controlChannel ?: return
        if (channel.state() != DataChannel.State.OPEN) return
        val payload = ByteBuffer.allocate(13).order(ByteOrder.LITTLE_ENDIAN)
            .put(1).putInt(padIndex).putInt(index).putInt(encodeNetworkButtonRange(range, pressed))
            .apply { flip() }
        channel.send(DataChannel.Buffer(payload, true))
    }

    fun disconnect() {
        scope.launch { closeSession(deleteRoom = true); _state.value = RemotePlayState() }
    }

    private fun startScreenCapture(context: Context, connection: PeerConnection, permission: Intent) {
        val source = requireNotNull(factory).createVideoSource(true)
        val screenCapturer = ScreenCapturerAndroid(
            permission,
            object : android.media.projection.MediaProjection.Callback() {
                override fun onStop() { disconnect() }
            }
        )
        val helper = SurfaceTextureHelper.create("RemotePlayCapture", eglContext)
        screenCapturer.initialize(helper, context.applicationContext, source.capturerObserver)
        screenCapturer.startCapture(VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_FPS)
        val track = requireNotNull(factory).createVideoTrack("remote-play-screen", source)
        connection.addTrack(track, listOf("remote-play"))
        capturer = screenCapturer
        textureHelper = helper
    }

    private fun createPeer(isHost: Boolean, servers: List<SignalIceServer>): PeerConnection {
        val config = PeerConnection.RTCConfiguration(servers.map(SignalIceServer::toWebRtc)).apply {
            sdpSemantics = PeerConnection.SdpSemantics.UNIFIED_PLAN
            continualGatheringPolicy = PeerConnection.ContinualGatheringPolicy.GATHER_CONTINUALLY
        }
        val connection = factory?.createPeerConnection(config, object : RemotePeerObserver() {
            override fun onIceCandidate(candidate: IceCandidate) = publishCandidate(candidate)
            override fun onDataChannel(channel: DataChannel) {
                if (!isHost && channel.label() == CONTROL_LABEL) bindControlChannel(channel)
            }
            override fun onAddTrack(receiver: RtpReceiver, streams: Array<out MediaStream>) {
                (receiver.track() as? VideoTrack)?.let { remoteTrack = it; _remoteVideoTrack.value = it }
            }
            override fun onConnectionChange(newState: PeerConnection.PeerConnectionState) {
                if (closing) return
                when (newState) {
                    PeerConnection.PeerConnectionState.CONNECTED ->
                        _state.value = _state.value.copy(status = RemotePlayStatus.Connected, error = null)
                    PeerConnection.PeerConnectionState.FAILED,
                    PeerConnection.PeerConnectionState.CLOSED -> fail(InternetLinkError.ConnectionFailed)
                    else -> Unit
                }
            }
        }) ?: error("Unable to create Remote Play peer")
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
                        peer?.setRemoteRemoteDescriptionAwait(state.answer.toWebRtc())
                        remoteDescriptionApplied = true
                        flushCandidates()
                        _state.value = _state.value.copy(status = RemotePlayStatus.Connecting)
                    }
                    state.candidates.forEach { candidate ->
                        lastCandidateId = maxOf(lastCandidateId, candidate.id)
                        val value = candidate.toWebRtc()
                        if (remoteDescriptionApplied) peer?.addIceCandidate(value)
                        else synchronized(pendingCandidates) { pendingCandidates += value }
                    }
                } catch (error: SignalingException) {
                    if (error.status == 404 || error.status == 403) {
                        fail(InternetLinkError.ConnectionFailed)
                        return@launch
                    }
                } catch (_: Throwable) {
                    // Retry transient signaling failures while the media connection is active.
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

    private fun bindControlChannel(channel: DataChannel?) {
        channel ?: return
        controlChannel = channel
        channel.registerObserver(object : DataChannel.Observer {
            override fun onBufferedAmountChange(previousAmount: Long) = Unit
            override fun onStateChange() = Unit
            override fun onMessage(buffer: DataChannel.Buffer) {
                if (!buffer.binary || !_state.value.isHost || buffer.data.remaining() != 13) return
                val payload = buffer.data.order(ByteOrder.LITTLE_ENDIAN)
                if (payload.get().toInt() != 1) return
                val padIndex = payload.int
                val buttonIndex = payload.int
                val range = payload.int
                NativeApp.setPadButton(padIndex, buttonIndex, range, range > 0)
            }
        })
    }

    private fun flushCandidates() {
        val copy = synchronized(pendingCandidates) { pendingCandidates.toList().also { pendingCandidates.clear() } }
        copy.forEach { peer?.addIceCandidate(it) }
    }

    private suspend fun closeSession(deleteRoom: Boolean) {
        closing = true
        val code = roomCode
        val token = roomToken
        val wasHost = _state.value.isHost
        signalingPoll?.cancel(); signalingPoll = null
        controlChannel?.unregisterObserver(); controlChannel?.close(); controlChannel = null
        peer?.close(); peer = null
        runCatching { capturer?.stopCapture() }
        capturer?.dispose(); capturer = null
        textureHelper?.dispose(); textureHelper = null
        remoteTrack = null
        _remoteVideoTrack.value = null
        if (deleteRoom && wasHost && code.isNotBlank() && token.isNotBlank()) {
            runCatching { MultiplayerSignalingClient.deleteRoom(code, token) }
        }
        if (wasHost) applicationContext?.let(RemotePlayCaptureService::stop)
        roomCode = ""; roomToken = ""; lastCandidateId = 0
        remoteDescriptionApplied = false
        synchronized(pendingCandidates) { pendingCandidates.clear() }
        synchronized(pendingLocalCandidates) { pendingLocalCandidates.clear() }
        closing = false
    }

    private fun fail(error: InternetLinkError) {
        if (_state.value.isHost) applicationContext?.let(RemotePlayCaptureService::stop)
        _state.value = _state.value.copy(status = RemotePlayStatus.Error, error = error)
    }

    fun sanitizeCode(value: String) = sanitizeMultiplayerRoomCode(value)
}

private abstract class RemotePeerObserver : PeerConnection.Observer {
    override fun onSignalingChange(state: PeerConnection.SignalingState) = Unit
    override fun onIceConnectionChange(state: PeerConnection.IceConnectionState) = Unit
    override fun onIceConnectionReceivingChange(receiving: Boolean) = Unit
    override fun onIceGatheringChange(state: PeerConnection.IceGatheringState) = Unit
    override fun onIceCandidatesRemoved(candidates: Array<out IceCandidate>) = Unit
    override fun onAddStream(stream: MediaStream) = Unit
    override fun onRemoveStream(stream: MediaStream) = Unit
    override fun onRenegotiationNeeded() = Unit
}

private suspend fun PeerConnection.createRemoteOfferAwait(): SessionDescription =
    suspendCancellableCoroutine { createOffer(RemoteSdpContinuation(it), MediaConstraints()) }
private suspend fun PeerConnection.createRemoteAnswerAwait(): SessionDescription =
    suspendCancellableCoroutine { createAnswer(RemoteSdpContinuation(it), MediaConstraints()) }
private suspend fun PeerConnection.setRemoteLocalDescriptionAwait(value: SessionDescription) =
    suspendCancellableCoroutine<Unit> { setLocalDescription(RemoteSetSdpContinuation(it), value) }
private suspend fun PeerConnection.setRemoteRemoteDescriptionAwait(value: SessionDescription) =
    suspendCancellableCoroutine<Unit> { setRemoteDescription(RemoteSetSdpContinuation(it), value) }

private class RemoteSdpContinuation(
    private val continuation: kotlinx.coroutines.CancellableContinuation<SessionDescription>
) : SdpObserver {
    override fun onCreateSuccess(value: SessionDescription) = continuation.resume(value)
    override fun onCreateFailure(error: String) = continuation.resumeWithException(IllegalStateException(error))
    override fun onSetSuccess() = Unit
    override fun onSetFailure(error: String) = Unit
}

private class RemoteSetSdpContinuation(
    private val continuation: kotlinx.coroutines.CancellableContinuation<Unit>
) : SdpObserver {
    override fun onCreateSuccess(value: SessionDescription) = Unit
    override fun onCreateFailure(error: String) = Unit
    override fun onSetSuccess() = continuation.resume(Unit)
    override fun onSetFailure(error: String) = continuation.resumeWithException(IllegalStateException(error))
}
