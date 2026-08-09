// SPDX-License-Identifier: GPL-3.0+

#include "Host/AudioStream.h"

#include "common/Assertions.h"
#include "common/Error.h"

#include <aaudio/AAudio.h>
#include <android/log.h>

#include <algorithm>
#include <atomic>

namespace
{
constexpr const char* LOG_TAG = "EmuCoreX";
constexpr int64_t STATE_CHANGE_TIMEOUT_NANOS = 500'000'000;

class AndroidAAudioStream final : public AudioStream
{
public:
	AndroidAAudioStream(u32 sample_rate, const AudioStreamParameters& parameters)
		: AudioStream(sample_rate, parameters)
	{
	}

	~AndroidAAudioStream() override
	{
		CloseDevice();
	}

	void SetPaused(bool paused) override
	{
		if (!m_stream)
		{
			if (!paused && m_disconnected.load(std::memory_order_acquire))
			{
				if (ReopenDevice())
				{
					m_paused = false;
					m_callback_silent.store(false, std::memory_order_release);
				}
			}
			return;
		}

		if (!paused && m_disconnected.load(std::memory_order_acquire))
		{
			if (!ReopenDevice())
				return;
			m_paused = false;
			m_callback_silent.store(false, std::memory_order_release);
			return;
		}

		if (m_paused == paused)
			return;

		// AAudio state changes are asynchronous. In particular, requestStart() is not valid while a
		// preceding requestPause() is still in PAUSING. The in-game menu can be opened and closed in
		// consecutive frames, so issuing both requests without completing the pause transition can
		// leave the device paused while m_paused says it is running. Every later resume then becomes a
		// no-op and audio remains silent until the VM is restarted.
		if (!paused && !WaitForPendingPause())
		{
			if (m_disconnected.load(std::memory_order_acquire))
				ReopenDevice();
			return;
		}
		if (!paused)
		{
			const aaudio_stream_state_t state = AAudioStream_getState(m_stream);
			if (state == AAUDIO_STREAM_STATE_STARTED || state == AAUDIO_STREAM_STATE_STARTING)
			{
				// A pause request can lose a race with the initial STARTING transition.
				// The callback was still silenced, so resuming only needs to make it audible.
				m_paused = false;
				m_callback_silent.store(false, std::memory_order_release);
				return;
			}
		}

		if (paused)
			m_callback_silent.store(true, std::memory_order_release);

		const aaudio_result_t result = paused ? AAudioStream_requestPause(m_stream) : AAudioStream_requestStart(m_stream);
		if (result != AAUDIO_OK)
		{
			__android_log_print(ANDROID_LOG_WARN, LOG_TAG, "AAudio pause/start failed: %s", AAudio_convertResultToText(result));
			if (AAudioStream_getState(m_stream) == AAUDIO_STREAM_STATE_DISCONNECTED)
				m_disconnected.store(true, std::memory_order_release);
			if (paused)
				m_paused = true;
			return;
		}

		m_paused = paused;
		if (!paused)
			m_callback_silent.store(false, std::memory_order_release);
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
		return StartAAudioDevice(error);
	}

private:
	bool OpenAAudioDevice(Error* error)
	{
		pxAssert(!m_stream);

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
		AAudioStreamBuilder_setDataCallback(builder, &AndroidAAudioStream::DataCallback, this);
		AAudioStreamBuilder_setErrorCallback(builder, &AndroidAAudioStream::ErrorCallback, this);

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
		m_disconnected.store(false, std::memory_order_release);
		m_callback_silent.store(false, std::memory_order_release);

		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
			"AAudio stream started rate=%u channels=%u output_buffer=%d/%d frames internal_buffer=%u ms minimal=%d",
			m_sample_rate, static_cast<unsigned>(m_output_channels), AAudioStream_getBufferSizeInFrames(m_stream),
			AAudioStream_getBufferCapacityInFrames(m_stream), static_cast<unsigned>(m_parameters.buffer_ms),
			m_parameters.minimal_output_latency ? 1 : 0);
		return true;
	}

	bool ReopenDevice()
	{
		CloseDevice();
		Error error;
		if (!OpenAAudioDevice(&error) || !StartAAudioDevice(&error))
		{
			__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "AAudio stream recovery failed: %s",
				error.GetDescription().c_str());
			return false;
		}

		__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "AAudio stream recovered after disconnect");
		return true;
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
				m_disconnected.store(true, std::memory_order_release);
			__android_log_print(ANDROID_LOG_WARN, LOG_TAG,
				"AAudio cannot resume from stream state %d", static_cast<int>(state));
			return false;
		}

		return true;
	}

	void CloseDevice()
	{
		if (!m_stream)
			return;

		AAudioStream_requestStop(m_stream);
		AAudioStream_close(m_stream);
		m_stream = nullptr;
	}

	static aaudio_data_callback_result_t DataCallback(
		AAudioStream*, void* userdata, void* audio_data, int32_t num_frames)
	{
		AndroidAAudioStream* stream = static_cast<AndroidAAudioStream*>(userdata);
		if (!stream || num_frames <= 0)
			return AAUDIO_CALLBACK_RESULT_CONTINUE;
		if (stream->m_callback_silent.load(std::memory_order_acquire))
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
		AndroidAAudioStream* stream = static_cast<AndroidAAudioStream*>(userdata);
		if (stream && error == AAUDIO_ERROR_DISCONNECTED)
		{
			stream->m_callback_silent.store(true, std::memory_order_release);
			stream->m_disconnected.store(true, std::memory_order_release);
		}
		__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "AAudio stream error: %s", AAudio_convertResultToText(error));
	}

	AAudioStream* m_stream = nullptr;
	std::atomic_bool m_callback_silent{false};
	std::atomic_bool m_disconnected{false};
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
