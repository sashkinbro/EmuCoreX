// SPDX-License-Identifier: GPL-3.0+
#include "common/AndroidLogging.h"

#include "Host/AudioStream.h"

#include "common/Assertions.h"
#include "common/Error.h"

#include <aaudio/AAudio.h>
#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace
{
constexpr const char* LOG_TAG = "EmuCoreX";
constexpr int64_t STATE_CHANGE_TIMEOUT_NANOS = 500'000'000;
constexpr auto RECOVERY_RETRY_DELAY = std::chrono::milliseconds(250);

class AndroidAAudioStream final : public AudioStream
{
    static constexpr u64 CALLBACK_SILENT = 1;
    static constexpr u64 CALLBACK_RECOVERY_PENDING = 2;
    static constexpr u64 CALLBACK_FLAGS = CALLBACK_SILENT | CALLBACK_RECOVERY_PENDING;
    struct CallbackContext
    {
        AndroidAAudioStream* owner;
        u64 generation;
    };

public:
	AndroidAAudioStream(u32 sample_rate, const AudioStreamParameters& parameters)
		: AudioStream(sample_rate, parameters)
	{
	}

	~AndroidAAudioStream() override
	{
		{
			const std::lock_guard lock(m_device_mutex);
			m_shutdown = true;
		}
		m_recovery_cv.notify_one();
		if (m_recovery_thread.joinable())
			m_recovery_thread.join();
		CloseDevice();
	}

	void SetPaused(bool paused) override
	{
		const std::lock_guard lock(m_device_mutex);
		if (m_shutdown)
			return;
		const bool was_paused = m_paused;
		m_paused = paused; // Desired state also survives a failed device reopen.
		if (paused)
			SetCallbackSilent(true);
		m_recovery_cv.notify_one();
		if (!m_stream || RecoveryPending())
		{
			if (!paused)
				RequestRecovery();
			return;
		}
		if (was_paused == paused)
			return;

		// AAudio state changes are asynchronous. In particular, requestStart() is not valid while a
		// preceding requestPause() is still in PAUSING. The in-game menu can be opened and closed in
		// consecutive frames, so issuing both requests without completing the pause transition can
		// leave the device paused while m_paused says it is running. Every later resume then becomes a
		// no-op and audio remains silent until the VM is restarted.
		if (!paused && !WaitForPendingPause())
		{
			RequestRecovery();
			return;
		}
		if (!paused)
		{
			const aaudio_stream_state_t state = AAudioStream_getState(m_stream);
			if (state == AAUDIO_STREAM_STATE_STARTED || state == AAUDIO_STREAM_STATE_STARTING)
			{
				// A pause request can lose a race with the initial STARTING transition.
				// The callback was still silenced, so resuming only needs to make it audible.
				SetCallbackSilent(false);
				return;
			}
		}

		const aaudio_result_t result = paused ? AAudioStream_requestPause(m_stream) : AAudioStream_requestStart(m_stream);
		if (result != AAUDIO_OK)
		{
			__android_log_print(ANDROID_LOG_WARN, LOG_TAG, "AAudio pause/start failed: %s", AAudio_convertResultToText(result));
			if (!paused || AAudioStream_getState(m_stream) == AAUDIO_STREAM_STATE_DISCONNECTED)
				RequestRecovery();
			return;
		}

		if (!paused)
			SetCallbackSilent(false);
	}

	bool OpenDevice(bool stretch_enabled, Error* error)
	{
		pxAssert(!m_stream);

		static constexpr const std::array<SampleReader, static_cast<size_t>(AudioExpansionMode::Count)> sample_readers = {{
			&StereoSampleReaderImpl,
			&SampleReaderImpl<AudioExpansionMode::StereoLFE, READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT, READ_CHANNEL_LFE>,
			&SampleReaderImpl<AudioExpansionMode::Quadraphonic, READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT,
				READ_CHANNEL_REAR_LEFT, READ_CHANNEL_REAR_RIGHT>,
			&SampleReaderImpl<AudioExpansionMode::QuadraphonicLFE, READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT,
				READ_CHANNEL_LFE, READ_CHANNEL_REAR_LEFT, READ_CHANNEL_REAR_RIGHT>,
			&SampleReaderImpl<AudioExpansionMode::Surround51, READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT,
				READ_CHANNEL_FRONT_CENTER, READ_CHANNEL_LFE, READ_CHANNEL_REAR_LEFT, READ_CHANNEL_REAR_RIGHT>,
			&SampleReaderImpl<AudioExpansionMode::Surround71, READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT,
				READ_CHANNEL_FRONT_CENTER, READ_CHANNEL_LFE, READ_CHANNEL_SIDE_LEFT, READ_CHANNEL_SIDE_RIGHT,
				READ_CHANNEL_REAR_LEFT, READ_CHANNEL_REAR_RIGHT>,
		}};

		if (!OpenAAudioDevice(error))
			return false;

		// Initialize the shared mixer buffers before requestStart(), since AAudio may
		// invoke the data callback immediately after the stream starts.
		BaseInitialize(sample_readers[static_cast<size_t>(m_parameters.expansion_mode)], stretch_enabled);
		if (!StartAAudioDevice(error))
			return false;
		m_recovery_thread = std::thread(&AndroidAAudioStream::RecoveryThread, this);
		return true;
	}

private:
	bool OpenAAudioDevice(Error* error)
	{
		pxAssert(!m_stream);
		BeginCallbackGeneration();
		m_callback_context = std::make_unique<CallbackContext>(CallbackContext{this, m_generation});

		AAudioStreamBuilder* builder = nullptr;
		aaudio_result_t result = AAudio_createStreamBuilder(&builder);
		if (result != AAUDIO_OK || !builder)
		{
			Error::SetStringFmt(error, "AAudio_createStreamBuilder() failed: {}", AAudio_convertResultToText(result));
			return false;
		}

		AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
		AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
		AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
		AAudioStreamBuilder_setSampleRate(builder, static_cast<int32_t>(m_sample_rate));
		AAudioStreamBuilder_setChannelCount(builder, static_cast<int32_t>(m_output_channels));
		AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
		const int32_t requested_output_frames = static_cast<int32_t>(AudioStream::GetBufferSizeForMS(
			m_sample_rate, m_parameters.output_latency_ms));
		if (!m_parameters.minimal_output_latency)
			AAudioStreamBuilder_setBufferCapacityInFrames(builder, requested_output_frames);
		AAudioStreamBuilder_setDataCallback(builder, &AndroidAAudioStream::DataCallback, m_callback_context.get());
		AAudioStreamBuilder_setErrorCallback(builder, &AndroidAAudioStream::ErrorCallback, m_callback_context.get());

		result = AAudioStreamBuilder_openStream(builder, &m_stream);
		AAudioStreamBuilder_delete(builder);
		if (result != AAUDIO_OK || !m_stream)
		{
			Error::SetStringFmt(error, "AAudioStreamBuilder_openStream() failed: {}", AAudio_convertResultToText(result));
			m_stream = nullptr;
			return false;
		}

		const int32_t target_output_frames = m_parameters.minimal_output_latency ?
			std::max(AAudioStream_getFramesPerBurst(m_stream) * 2, 1) : requested_output_frames;
		const aaudio_result_t buffer_size_result = AAudioStream_setBufferSizeInFrames(m_stream, target_output_frames);
		if (buffer_size_result < 0)
		{
			__android_log_print(ANDROID_LOG_WARN, LOG_TAG, "AAudio buffer resize to %d frames failed: %s",
				target_output_frames, AAudio_convertResultToText(buffer_size_result));
		}

		return true;
	}

	bool StartAAudioDevice(Error* error)
	{
		const aaudio_result_t result = AAudioStream_requestStart(m_stream);
		if (result != AAUDIO_OK)
		{
			Error::SetStringFmt(error, "AAudioStream_requestStart() failed: {}", AAudio_convertResultToText(result));
			CloseDevice();
			return false;
		}
		// Do not clear a disconnect reported during requestStart().
		SetCallbackSilent(m_paused);

		EMUCOREX_DIAGNOSTIC_LOG_PRINT(ANDROID_LOG_INFO, LOG_TAG,
			"AAudio stream started rate=%u channels=%u output_buffer=%d/%d frames internal_buffer=%u ms minimal=%d",
			m_sample_rate, static_cast<unsigned>(m_output_channels), AAudioStream_getBufferSizeInFrames(m_stream),
			AAudioStream_getBufferCapacityInFrames(m_stream), static_cast<unsigned>(m_parameters.buffer_ms),
			m_parameters.minimal_output_latency ? 1 : 0);
		return true;
	}

	bool ReopenDevice(Error* error)
	{
		CloseDevice();
		if (!OpenAAudioDevice(error) || !StartAAudioDevice(error))
			return false;
		if (RecoveryPending())
		{
			Error::SetStringFmt(error, "AAudio disconnected while restarting");
			return false;
		}

		EMUCOREX_DIAGNOSTIC_LOG_WRITE(ANDROID_LOG_INFO, LOG_TAG, "AAudio stream recovered after disconnect");
		return true;
	}

	bool RecoveryPending() const
	{
		return (m_callback_state.load(std::memory_order_acquire) & CALLBACK_RECOVERY_PENDING) != 0;
	}

	void RequestRecovery()
	{
		m_callback_state.fetch_or(CALLBACK_SILENT | CALLBACK_RECOVERY_PENDING, std::memory_order_acq_rel);
		m_recovery_cv.notify_one();
	}

	void SetCallbackSilent(bool silent)
	{
		if (silent)
		{
			m_callback_state.fetch_or(CALLBACK_SILENT, std::memory_order_acq_rel);
			return;
		}
		u64 state = m_callback_state.load(std::memory_order_acquire);
		while (!(state & CALLBACK_RECOVERY_PENDING) &&
			!m_callback_state.compare_exchange_weak(state, state & ~CALLBACK_SILENT,
				std::memory_order_acq_rel, std::memory_order_acquire))
		{
		}
	}

	void BeginCallbackGeneration()
	{
		m_generation += CALLBACK_FLAGS + 1;
		m_callback_state.store(m_generation | CALLBACK_SILENT, std::memory_order_release);
	}

	void RecoveryThread()
	{
		std::unique_lock lock(m_device_mutex);
		std::chrono::steady_clock::time_point next_attempt{};
		u32 failures = 0;
		while (!m_shutdown)
		{
			// Error callbacks only publish atomics and notify. A bounded wait
			// also covers notification before the worker actually begins waiting.
			m_recovery_cv.wait_for(lock, RECOVERY_RETRY_DELAY,
				[this] { return m_shutdown || (!m_paused && RecoveryPending()); });
			if (m_shutdown)
				break;
			if (m_paused || !RecoveryPending())
				continue;
			if (std::chrono::steady_clock::now() < next_attempt)
			{
				m_recovery_cv.wait_until(lock, next_attempt, [this] { return m_shutdown || m_paused; });
				continue;
			}

			Error error;
			if (ReopenDevice(&error))
			{
				failures = 0;
				next_attempt = {};
			}
			else
			{
				RequestRecovery();
				next_attempt = std::chrono::steady_clock::now() + RECOVERY_RETRY_DELAY;
				if (++failures == 1 || (failures % 20) == 0)
					__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "AAudio stream recovery failed (retry %u): %s",
						failures, error.GetDescription().c_str());
			}
		}
	}

	bool WaitForPendingPause()
	{
		aaudio_stream_state_t state = AAudioStream_getState(m_stream);
		while (state == AAUDIO_STREAM_STATE_PAUSING)
		{
			aaudio_stream_state_t next_state = state;
			const aaudio_result_t result = AAudioStream_waitForStateChange(
				m_stream, state, &next_state, STATE_CHANGE_TIMEOUT_NANOS);
			if (result != AAUDIO_OK)
			{
				__android_log_print(ANDROID_LOG_WARN, LOG_TAG,
					"AAudio pause transition did not complete: %s", AAudio_convertResultToText(result));
				return false;
			}
			state = next_state;
		}

		if (state == AAUDIO_STREAM_STATE_DISCONNECTED || state == AAUDIO_STREAM_STATE_CLOSING ||
			state == AAUDIO_STREAM_STATE_CLOSED)
		{
			if (state == AAUDIO_STREAM_STATE_DISCONNECTED)
				RequestRecovery();
			__android_log_print(ANDROID_LOG_WARN, LOG_TAG,
				"AAudio cannot resume from stream state %d", static_cast<int>(state));
			return false;
		}

		return true;
	}

	void CloseDevice()
	{
		// Invalidate the old generation before stop/close can deliver a late
		// callback. Its context stays alive until AAudio_close has completed.
		BeginCallbackGeneration();
		if (!m_stream)
		{
			m_callback_context.reset();
			return;
		}

		AAudioStream_requestStop(m_stream);
		AAudioStream_close(m_stream);
		m_stream = nullptr;
		m_callback_context.reset();
	}

	static aaudio_data_callback_result_t DataCallback(
		AAudioStream*, void* userdata, void* audio_data, int32_t num_frames)
	{
		const CallbackContext* context = static_cast<const CallbackContext*>(userdata);
		if (!context || num_frames <= 0)
			return AAUDIO_CALLBACK_RESULT_CONTINUE;
		AndroidAAudioStream* stream = context->owner;
		const u64 state = stream->m_callback_state.load(std::memory_order_acquire);
		if ((state & ~CALLBACK_FLAGS) != context->generation || (state & CALLBACK_SILENT))
		{
			std::fill_n(static_cast<SampleType*>(audio_data),
				static_cast<size_t>(num_frames) * stream->m_output_channels, static_cast<SampleType>(0));
			return AAUDIO_CALLBACK_RESULT_CONTINUE;
		}

		stream->ReadFrames(static_cast<SampleType*>(audio_data), static_cast<u32>(num_frames));
		return AAUDIO_CALLBACK_RESULT_CONTINUE;
	}

	static void ErrorCallback(AAudioStream*, void* userdata, aaudio_result_t error)
	{
		const CallbackContext* context = static_cast<const CallbackContext*>(userdata);
		if (context && error < 0)
		{
			AndroidAAudioStream* stream = context->owner;
			u64 state = stream->m_callback_state.load(std::memory_order_acquire);
			while ((state & ~CALLBACK_FLAGS) == context->generation)
			{
				if (stream->m_callback_state.compare_exchange_weak(state,
					state | CALLBACK_SILENT | CALLBACK_RECOVERY_PENDING,
					std::memory_order_acq_rel, std::memory_order_acquire))
				{
					stream->m_recovery_cv.notify_one();
					break;
				}
			}
		}
		__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "AAudio stream error: %s", AAudio_convertResultToText(error));
	}

	AAudioStream* m_stream = nullptr;
	std::unique_ptr<CallbackContext> m_callback_context;
	std::atomic<u64> m_callback_state{CALLBACK_SILENT};
	u64 m_generation = 0; // Only touched while owning the device lifecycle.
	std::mutex m_device_mutex;
	std::condition_variable m_recovery_cv;
	std::thread m_recovery_thread;
	bool m_shutdown = false;
};
} // namespace

std::unique_ptr<AudioStream> AudioStream::CreateSDLAudioStream(u32 sample_rate, const AudioStreamParameters& parameters,
	bool stretch_enabled, Error* error)
{
	std::unique_ptr<AndroidAAudioStream> stream = std::make_unique<AndroidAAudioStream>(sample_rate, parameters);
	if (!stream->OpenDevice(stretch_enabled, error))
		stream.reset();
	return stream;
}
