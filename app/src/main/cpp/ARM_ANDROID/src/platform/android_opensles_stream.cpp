// SPDX-License-Identifier: GPL-3.0+

#include "Host/AudioStream.h"

#include "common/Assertions.h"
#include "common/Error.h"

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <SLES/OpenSLES_AndroidConfiguration.h>
#include <android/log.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <vector>

namespace
{
constexpr const char* LOG_TAG = "EmuCoreX";

// Two buffers keep the implementation small while still absorbing one scheduling hiccup.
static constexpr size_t NUM_BUFFERS = 2;
static constexpr u32 MINIMAL_BUFFER_MS = 10;

class AndroidOpenSLESStream final : public AudioStream
{
public:
	AndroidOpenSLESStream(u32 sample_rate, const AudioStreamParameters& parameters)
		: AudioStream(sample_rate, parameters)
	{
		// Android only guarantees portable OpenSL ES buffer-queue playback for mono or stereo.
		// The emulator's unexpanded input is already stereo, so bypass surround expansion instead
		// of discarding its center/rear channels after doing unnecessary mixing work.
		if (m_parameters.expansion_mode != AudioExpansionMode::Disabled)
		{
			__android_log_print(ANDROID_LOG_WARN, LOG_TAG,
				"OpenSL ES uses stereo output; surround expansion has been disabled for this stream");
		}
		m_parameters.expansion_mode = AudioExpansionMode::Disabled;
		m_internal_channels = 2;
		m_output_channels = 2;
	}

	~AndroidOpenSLESStream() override
	{
		CloseDevice();
	}

	void SetPaused(bool paused) override
	{
		if (m_paused == paused || !m_player_play)
			return;

		if (paused)
			m_callback_paused.store(true, std::memory_order_release);

		const SLuint32 state = paused ? SL_PLAYSTATE_PAUSED : SL_PLAYSTATE_PLAYING;
		const SLresult result = (*m_player_play)->SetPlayState(m_player_play, state);
		if (result != SL_RESULT_SUCCESS)
		{
			if (paused)
				m_callback_paused.store(false, std::memory_order_release);
			__android_log_print(ANDROID_LOG_WARN, LOG_TAG, "OpenSL ES pause/start failed: %u", result);
			return;
		}

		m_paused = paused;
		if (!paused)
			m_callback_paused.store(false, std::memory_order_release);
	}

	bool OpenDevice(bool stretch_enabled, Error* error)
	{
		pxAssert(!m_engine_obj);

		const u32 output_buffer_ms = m_parameters.minimal_output_latency ?
			MINIMAL_BUFFER_MS : std::max<u32>(m_parameters.output_latency_ms, 1u);
		m_frames_per_buffer = GetBufferSizeForMS(m_sample_rate, output_buffer_ms);
		const size_t samples_per_buffer = static_cast<size_t>(m_frames_per_buffer) * m_output_channels;
		m_buffer_bytes = static_cast<u32>(samples_per_buffer * sizeof(SLint16));
		m_float_buffer.resize(samples_per_buffer, 0.0f);
		for (std::vector<SLint16>& buffer : m_audio_buffers)
			buffer.resize(samples_per_buffer, 0);
		m_next_buffer = 0;

		SLresult result = slCreateEngine(&m_engine_obj, 0, nullptr, 0, nullptr, nullptr);
		if (result != SL_RESULT_SUCCESS)
		{
			Error::SetStringFmt(error, "slCreateEngine() failed: {}", static_cast<unsigned>(result));
			return false;
		}

		result = (*m_engine_obj)->Realize(m_engine_obj, SL_BOOLEAN_FALSE);
		if (result != SL_RESULT_SUCCESS)
		{
			Error::SetStringFmt(error, "OpenSL ES engine realization failed: {}", static_cast<unsigned>(result));
			CloseDevice();
			return false;
		}

		result = (*m_engine_obj)->GetInterface(m_engine_obj, SL_IID_ENGINE, &m_engine);
		if (result != SL_RESULT_SUCCESS)
		{
			Error::SetStringFmt(error, "OpenSL ES engine interface failed: {}", static_cast<unsigned>(result));
			CloseDevice();
			return false;
		}

		result = (*m_engine)->CreateOutputMix(m_engine, &m_output_mix_obj, 0, nullptr, nullptr);
		if (result != SL_RESULT_SUCCESS)
		{
			Error::SetStringFmt(error, "OpenSL ES output mix creation failed: {}", static_cast<unsigned>(result));
			CloseDevice();
			return false;
		}

		result = (*m_output_mix_obj)->Realize(m_output_mix_obj, SL_BOOLEAN_FALSE);
		if (result != SL_RESULT_SUCCESS)
		{
			Error::SetStringFmt(error, "OpenSL ES output mix realization failed: {}", static_cast<unsigned>(result));
			CloseDevice();
			return false;
		}

		// PCM16 is supported by every Android OpenSL ES implementation. Float PCM is optional
		// and causes CreateAudioPlayer() to fail on a number of older or vendor-modified devices.
		SLDataFormat_PCM pcm_format = {
			SL_DATAFORMAT_PCM,
			static_cast<SLuint32>(m_output_channels),
			static_cast<SLuint32>(m_sample_rate) * 1000u,
			SL_PCMSAMPLEFORMAT_FIXED_16,
			SL_PCMSAMPLEFORMAT_FIXED_16,
			SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
			SL_BYTEORDER_LITTLEENDIAN,
		};
		SLDataLocator_AndroidSimpleBufferQueue buffer_queue_locator = {
			SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
			static_cast<SLuint32>(NUM_BUFFERS),
		};
		SLDataSource audio_source = {
			&buffer_queue_locator,
			&pcm_format,
		};
		SLDataLocator_OutputMix output_mix_locator = {SL_DATALOCATOR_OUTPUTMIX, m_output_mix_obj};
		SLDataSink audio_sink = {
			&output_mix_locator,
			nullptr,
		};

		const SLInterfaceID interface_ids[] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE, SL_IID_ANDROIDCONFIGURATION};
		const SLboolean required_interfaces[] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_FALSE};
		result = (*m_engine)->CreateAudioPlayer(m_engine, &m_player_obj, &audio_source, &audio_sink,
			std::size(interface_ids), interface_ids, required_interfaces);
		if (result != SL_RESULT_SUCCESS)
		{
			Error::SetStringFmt(error, "OpenSL ES audio player creation failed: {}", static_cast<unsigned>(result));
			CloseDevice();
			return false;
		}

		SLAndroidConfigurationItf player_config = nullptr;
		if ((*m_player_obj)->GetInterface(m_player_obj, SL_IID_ANDROIDCONFIGURATION, &player_config) == SL_RESULT_SUCCESS)
		{
			const SLuint32 performance_mode = m_parameters.minimal_output_latency ?
				SL_ANDROID_PERFORMANCE_LATENCY : SL_ANDROID_PERFORMANCE_POWER_SAVING;
			const SLresult config_result = (*player_config)->SetConfiguration(player_config,
				SL_ANDROID_KEY_PERFORMANCE_MODE, &performance_mode, sizeof(performance_mode));
			if (config_result != SL_RESULT_SUCCESS)
			{
				__android_log_print(ANDROID_LOG_WARN, LOG_TAG,
					"OpenSL ES performance mode request failed: %u", config_result);
			}
		}

		result = (*m_player_obj)->Realize(m_player_obj, SL_BOOLEAN_FALSE);
		if (result != SL_RESULT_SUCCESS)
		{
			Error::SetStringFmt(error, "OpenSL ES audio player realization failed: {}", static_cast<unsigned>(result));
			CloseDevice();
			return false;
		}

		result = (*m_player_obj)->GetInterface(m_player_obj, SL_IID_PLAY, &m_player_play);
		if (result != SL_RESULT_SUCCESS)
		{
			Error::SetStringFmt(error, "OpenSL ES play interface failed: {}", static_cast<unsigned>(result));
			CloseDevice();
			return false;
		}

		result = (*m_player_obj)->GetInterface(m_player_obj, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &m_buffer_queue);
		if (result != SL_RESULT_SUCCESS)
		{
			Error::SetStringFmt(error, "OpenSL ES buffer queue interface failed: {}", static_cast<unsigned>(result));
			CloseDevice();
			return false;
		}

		result = (*m_buffer_queue)->RegisterCallback(m_buffer_queue, BufferQueueCallback, this);
		if (result != SL_RESULT_SUCCESS)
		{
			Error::SetStringFmt(error, "OpenSL ES callback registration failed: {}", static_cast<unsigned>(result));
			CloseDevice();
			return false;
		}

		BaseInitialize(&StereoSampleReaderImpl, stretch_enabled);

		for (std::vector<SLint16>& buffer : m_audio_buffers)
		{
			result = (*m_buffer_queue)->Enqueue(m_buffer_queue, buffer.data(), m_buffer_bytes);
			if (result != SL_RESULT_SUCCESS)
			{
				Error::SetStringFmt(error, "OpenSL ES initial buffer enqueue failed: {}", static_cast<unsigned>(result));
				CloseDevice();
				return false;
			}
		}

		result = (*m_player_play)->SetPlayState(m_player_play, SL_PLAYSTATE_PLAYING);
		if (result != SL_RESULT_SUCCESS)
		{
			Error::SetStringFmt(error, "OpenSL ES playback start failed: {}", static_cast<unsigned>(result));
			CloseDevice();
			return false;
		}

		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
			"OpenSL ES stream started: rate=%u channels=%u buffer=%u frames x %zu internal=%u ms minimal=%d",
			m_sample_rate, static_cast<unsigned>(m_output_channels), m_frames_per_buffer, NUM_BUFFERS,
			static_cast<unsigned>(m_parameters.buffer_ms), m_parameters.minimal_output_latency ? 1 : 0);
		return true;
	}

private:
	void CloseDevice()
	{
		m_closing.store(true, std::memory_order_release);

		if (m_player_play)
			(*m_player_play)->SetPlayState(m_player_play, SL_PLAYSTATE_STOPPED);
		if (m_buffer_queue)
			(*m_buffer_queue)->Clear(m_buffer_queue);
		if (m_player_obj)
			(*m_player_obj)->Destroy(m_player_obj);
		if (m_output_mix_obj)
			(*m_output_mix_obj)->Destroy(m_output_mix_obj);
		if (m_engine_obj)
			(*m_engine_obj)->Destroy(m_engine_obj);

		m_buffer_queue = nullptr;
		m_player_play = nullptr;
		m_player_obj = nullptr;
		m_output_mix_obj = nullptr;
		m_engine = nullptr;
		m_engine_obj = nullptr;
	}

	static void BufferQueueCallback(SLAndroidSimpleBufferQueueItf, void* context)
	{
		AndroidOpenSLESStream* const stream = static_cast<AndroidOpenSLESStream*>(context);
		if (!stream || stream->m_closing.load(std::memory_order_acquire))
			return;

		std::vector<SLint16>& buffer = stream->m_audio_buffers[stream->m_next_buffer];
		stream->m_next_buffer = (stream->m_next_buffer + 1) % NUM_BUFFERS;
		if (stream->m_callback_paused.load(std::memory_order_acquire))
		{
			std::fill(buffer.begin(), buffer.end(), 0);
		}
		else
		{
			stream->ReadFrames(stream->m_float_buffer.data(), stream->m_frames_per_buffer);
			for (size_t i = 0; i < buffer.size(); i++)
			{
				const float sample = std::clamp(stream->m_float_buffer[i], -1.0f, 1.0f);
				buffer[i] = static_cast<SLint16>(sample * 32767.0f);
			}
		}

		if (stream->m_closing.load(std::memory_order_acquire))
			return;

		const SLresult result = (*stream->m_buffer_queue)->Enqueue(
			stream->m_buffer_queue, buffer.data(), stream->m_buffer_bytes);
		if (result != SL_RESULT_SUCCESS)
		{
			__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
				"OpenSL ES buffer enqueue failed: %u", result);
		}
	}

	SLObjectItf m_engine_obj = nullptr;
	SLEngineItf m_engine = nullptr;
	SLObjectItf m_output_mix_obj = nullptr;
	SLObjectItf m_player_obj = nullptr;
	SLPlayItf m_player_play = nullptr;
	SLAndroidSimpleBufferQueueItf m_buffer_queue = nullptr;

	std::array<std::vector<SLint16>, NUM_BUFFERS> m_audio_buffers;
	std::vector<SampleType> m_float_buffer;
	std::atomic_bool m_callback_paused{false};
	std::atomic_bool m_closing{false};
	u32 m_buffer_bytes = 0;
	u32 m_frames_per_buffer = 0;
	size_t m_next_buffer = 0;
};
} // namespace

std::unique_ptr<AudioStream> AudioStream::CreateOpenSLESAudioStream(
	u32 sample_rate, const AudioStreamParameters& parameters, bool stretch_enabled, Error* error)
{
	std::unique_ptr<AndroidOpenSLESStream> stream = std::make_unique<AndroidOpenSLESStream>(sample_rate, parameters);
	if (!stream->OpenDevice(stretch_enabled, error))
		stream.reset();
	return stream;
}
