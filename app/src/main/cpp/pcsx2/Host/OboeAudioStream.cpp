/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2021  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Host/AudioStream.h"

#include "common/Assertions.h"
#include "common/Console.h"
#include "common/Error.h"

#include "oboe/Oboe.h"

namespace {
    class OboeAudioStream : public AudioStream, oboe::AudioStreamDataCallback, oboe::AudioStreamErrorCallback
    {
    public:
        OboeAudioStream(u32 sample_rate, const AudioStreamParameters& parameters);
        ~OboeAudioStream() override;

        void SetPaused(bool paused) override;

        bool Initialize(bool stretch_enabled);
        bool Open();
        bool Start();
        void Stop();
        void Close();

        oboe::DataCallbackResult onAudioReady(oboe::AudioStream *p_audioStream,
                                              void *p_audioData, int32_t p_numFrames) override ;
        bool onError(oboe::AudioStream *oboeStream, oboe::Result error) override ;

    private:
        bool m_playing = false;
        bool m_stop_requested = false;

        std::shared_ptr<oboe::AudioStream> m_stream;
    };
}

oboe::DataCallbackResult
OboeAudioStream::onAudioReady(oboe::AudioStream *p_audioStream, void *p_audioData, int32_t p_numFrames) {
    if (p_audioData != nullptr) {
        ReadFrames(reinterpret_cast<SampleType*>(p_audioData), p_numFrames);
    }
    return oboe::DataCallbackResult::Continue;
}

bool OboeAudioStream::onError(oboe::AudioStream *oboeStream, oboe::Result error) {
    Console.Error("ErrorCB %d", error);
    if (error == oboe::Result::ErrorDisconnected && !m_stop_requested)
    {
        Console.Error("Audio stream disconnected, trying reopening...");
        Stop();
        Close();
        if (!Open() || !Start())
            Console.Error("Failed to reopen stream after disconnection.");

        return true;
    }

    return false;
}

bool OboeAudioStream::Initialize(bool stretch_enabled) {
    static constexpr const std::array<SampleReader, static_cast<size_t>(AudioExpansionMode::Count)> sample_readers = {{
         // Disabled
         &StereoSampleReaderImpl,
         // StereoLFE
         &SampleReaderImpl<AudioExpansionMode::StereoLFE, READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT,
         READ_CHANNEL_LFE>,
         // Quadraphonic
         &SampleReaderImpl<AudioExpansionMode::Quadraphonic, READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT,
         READ_CHANNEL_REAR_LEFT, READ_CHANNEL_REAR_RIGHT>,
         // QuadraphonicLFE
         &SampleReaderImpl<AudioExpansionMode::QuadraphonicLFE, READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT,
         READ_CHANNEL_LFE, READ_CHANNEL_REAR_LEFT, READ_CHANNEL_REAR_RIGHT>,
         // Surround51
         &SampleReaderImpl<AudioExpansionMode::Surround51, READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT,
         READ_CHANNEL_FRONT_CENTER, READ_CHANNEL_LFE, READ_CHANNEL_REAR_LEFT, READ_CHANNEL_REAR_RIGHT>,
         // Surround71
         &SampleReaderImpl<AudioExpansionMode::Surround71, READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT,
         READ_CHANNEL_FRONT_CENTER, READ_CHANNEL_LFE, READ_CHANNEL_SIDE_LEFT, READ_CHANNEL_SIDE_RIGHT,
         READ_CHANNEL_REAR_LEFT, READ_CHANNEL_REAR_RIGHT>,
     }};
    BaseInitialize(sample_readers[static_cast<size_t>(m_parameters.expansion_mode)], stretch_enabled);

    ////

    if (!Open()) {
        return false;
    }

    if (!Start()) {
        return false;
    }

    return true;
}

bool OboeAudioStream::Open() {
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output);
    builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
    builder.setSharingMode(oboe::SharingMode::Shared);
    builder.setFormat(oboe::AudioFormat::I16);
    builder.setSampleRate(m_sample_rate);
    builder.setChannelCount(m_output_channels==2 ? oboe::ChannelCount::Stereo : oboe::ChannelCount::Mono);
    builder.setDeviceId(oboe::kUnspecified);
    builder.setBufferCapacityInFrames(2048 * 2);
    builder.setFramesPerDataCallback(2048);
    builder.setDataCallback(this);
    builder.setErrorCallback(this);

    Console.WriteLn("(OboeMod) Creating stream...");
    oboe::Result result = builder.openStream(m_stream);
    if (result != oboe::Result::OK)
    {
        Console.Error("(OboeMod) openStream() failed: %d", result);
        return false;
    }

    return true;
}

bool OboeAudioStream::Start()
{
    if (m_playing)
        return true;

    Console.WriteLn("(OboeMod) Starting stream...");
    m_stop_requested = false;

    oboe::Result result = m_stream->requestStart();
    if (result != oboe::Result::OK)
    {
        Console.Error("(OboeMod) requestStart failed: %d", result);
        return false;
    }

    m_playing = true;
    return true;
}

void OboeAudioStream::Stop()
{
    if (!m_playing)
        return;

    Console.WriteLn("(OboeMod) Stopping stream...");
    m_stop_requested = true;

    oboe::Result result = m_stream->requestStop();
    if (result != oboe::Result::OK)
    {
        Console.Error("(OboeMod) requestStop() failed: %d", result);
        return;
    }

    m_playing = false;
}

void OboeAudioStream::Close()
{
    Console.WriteLn("(OboeMod) Closing stream...");

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

    if(paused) {
        oboe::Result result = m_stream->requestPause();
        if (result != oboe::Result::OK) {
            Console.Error("(OboeMod) requestStop() failed: %d", result);
            return;
        }
        m_playing = false;
    } else {
        Start();
    }

    m_paused = paused;
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
    if (!stream->Initialize(stretch_enabled)) {
        stream.reset();
    }
    return stream;
}
