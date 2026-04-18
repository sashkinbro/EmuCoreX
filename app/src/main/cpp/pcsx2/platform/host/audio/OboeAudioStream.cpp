// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "platform/host/audio/AudioStream.h"

#include "common/Assertions.h"
#include "common/Console.h"
#include "common/Error.h"

#include "oboe/Oboe.h"

namespace
{
	class OboeAudioStream final : public AudioStream, public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback
	{
	public:
		OboeAudioStream(u32 sample_rate, const AudioStreamParameters& parameters);
		~OboeAudioStream() override;

		void SetPaused(bool paused) override;

		bool Initialize(bool stretch_enabled, Error* error);

		oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audio_data, int32_t num_frames) override;
		bool onError(oboe::AudioStream* stream, oboe::Result error) override;

	private:
		bool Open(Error* error);
		bool Start(Error* error);
		void Stop();
		void Close();

		bool m_playing = false;
		bool m_stop_requested = false;
		std::shared_ptr<oboe::AudioStream> m_stream;
	};
}

OboeAudioStream::OboeAudioStream(u32 sample_rate, const AudioStreamParameters& parameters)
	: AudioStream(sample_rate, parameters)
{
}

OboeAudioStream::~OboeAudioStream()
{
	Close();
}

std::unique_ptr<AudioStream> AudioStream::CreateOboeAudioStream(u32 sample_rate, const AudioStreamParameters& parameters,
	bool stretch_enabled, Error* error)
{
	std::unique_ptr<OboeAudioStream> stream = std::make_unique<OboeAudioStream>(sample_rate, parameters);
	if (!stream->Initialize(stretch_enabled, error))
		stream.reset();

	return stream;
}

bool OboeAudioStream::Initialize(bool stretch_enabled, Error* error)
{
	static constexpr const std::array<SampleReader, static_cast<size_t>(AudioExpansionMode::Count)> sample_readers = {{
		&StereoSampleReaderImpl,
		&SampleReaderImpl<AudioExpansionMode::StereoLFE, READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT, READ_CHANNEL_LFE>,
		&SampleReaderImpl<AudioExpansionMode::Quadraphonic, READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT, READ_CHANNEL_REAR_LEFT, READ_CHANNEL_REAR_RIGHT>,
		&SampleReaderImpl<AudioExpansionMode::QuadraphonicLFE, READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT, READ_CHANNEL_LFE, READ_CHANNEL_REAR_LEFT, READ_CHANNEL_REAR_RIGHT>,
		&SampleReaderImpl<AudioExpansionMode::Surround51, READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT, READ_CHANNEL_FRONT_CENTER, READ_CHANNEL_LFE, READ_CHANNEL_REAR_LEFT, READ_CHANNEL_REAR_RIGHT>,
		&SampleReaderImpl<AudioExpansionMode::Surround71, READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT, READ_CHANNEL_FRONT_CENTER, READ_CHANNEL_LFE, READ_CHANNEL_SIDE_LEFT, READ_CHANNEL_SIDE_RIGHT, READ_CHANNEL_REAR_LEFT, READ_CHANNEL_REAR_RIGHT>,
	}};

	BaseInitialize(sample_readers[static_cast<size_t>(m_parameters.expansion_mode)], stretch_enabled);

	if (!Open(error))
		return false;

	if (!Start(error))
	{
		Close();
		return false;
	}

	return true;
}

bool OboeAudioStream::Open(Error* error)
{
	pxAssert(!m_stream);

	const u32 buffer_frames = GetBufferSizeForMS(m_sample_rate,
		std::max<u32>(m_parameters.buffer_ms, m_parameters.output_latency_ms));
	const u32 callback_frames = std::max<u32>(CHUNK_SIZE * 4,
		GetBufferSizeForMS(m_sample_rate, std::max<u32>(m_parameters.output_latency_ms, 10u)) / 2);

	oboe::AudioStreamBuilder builder;
	builder.setDirection(oboe::Direction::Output);
	builder.setPerformanceMode(m_parameters.minimal_output_latency ? oboe::PerformanceMode::LowLatency :
		oboe::PerformanceMode::None);
	builder.setSharingMode(oboe::SharingMode::Shared);
	builder.setUsage(oboe::Usage::Game);
	builder.setContentType(oboe::ContentType::Music);
	builder.setFormat(oboe::AudioFormat::Float);
	builder.setSampleRate(static_cast<int32_t>(m_sample_rate));
	builder.setChannelCount(static_cast<int32_t>(m_output_channels));
	builder.setDeviceId(oboe::kUnspecified);
	builder.setBufferCapacityInFrames(static_cast<int32_t>(buffer_frames));
	builder.setFramesPerDataCallback(static_cast<int32_t>(callback_frames));
	builder.setDataCallback(this);
	builder.setErrorCallback(this);

	const oboe::Result result = builder.openStream(m_stream);
	if (result != oboe::Result::OK)
	{
		Error::SetStringFmt(error, "oboe::AudioStreamBuilder::openStream() failed: {}", oboe::convertToText(result));
		return false;
	}

	return true;
}

bool OboeAudioStream::Start(Error* error)
{
	if (!m_stream || m_playing)
		return true;

	m_stop_requested = false;
	const oboe::Result result = m_stream->requestStart();
	if (result != oboe::Result::OK)
	{
		Error::SetStringFmt(error, "oboe stream requestStart() failed: {}", oboe::convertToText(result));
		return false;
	}

	m_playing = true;
	return true;
}

void OboeAudioStream::Stop()
{
	if (!m_stream || !m_playing)
		return;

	m_stop_requested = true;
	const oboe::Result result = m_stream->requestStop();
	(void)result;

	m_playing = false;
}

void OboeAudioStream::Close()
{
	if (m_playing)
		Stop();

	if (m_stream)
	{
		m_stream->close();
		m_stream.reset();
	}
}

void OboeAudioStream::SetPaused(bool paused)
{
	if (m_paused == paused)
		return;

	if (paused)
	{
		if (m_stream)
		{
			const oboe::Result result = m_stream->requestPause();
			(void)result;
		}
		m_playing = false;
	}
	else
	{
		Error error;
		Start(&error);
	}

	m_paused = paused;
}

oboe::DataCallbackResult OboeAudioStream::onAudioReady(oboe::AudioStream* stream, void* audio_data, int32_t num_frames)
{
	if (!audio_data || num_frames <= 0)
		return oboe::DataCallbackResult::Continue;

	ReadFrames(reinterpret_cast<SampleType*>(audio_data), static_cast<u32>(num_frames));
	return oboe::DataCallbackResult::Continue;
}

bool OboeAudioStream::onError(oboe::AudioStream* stream, oboe::Result error)
{
	if (error == oboe::Result::ErrorDisconnected && !m_stop_requested)
	{
		Close();
		Error reopen_error;
		if (!Open(&reopen_error) || !Start(&reopen_error))
			return true;
		return true;
	}

	return false;
}
