#include "sdl_audio.h"

#include <algorithm>
#include <cstdio>

namespace host
{
SdlAudio::~SdlAudio()
{
    if (m_stream)
        SDL_FreeAudioStream(m_stream);
    if (m_device)
        SDL_CloseAudioDevice(m_device);
    if (m_initialized)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SdlAudio::setMuted(bool muted)
{
    if (!muted && !m_device && !init(false))
    {
        m_muted = true; // no device: stay silent rather than pretend
        return;
    }

    if (m_device && muted != m_muted)
    {
        // Restart from a clean buffer so stale audio is never played after an
        // unmute, and so the prebuffer is rebuilt before playback resumes.
        SDL_PauseAudioDevice(m_device, 1);
        SDL_ClearQueuedAudio(m_device);
        m_started = false;
    }

    m_muted = muted;
}

bool SdlAudio::init(bool muted)
{
    m_muted = muted;
    if (m_muted)
        return true; // --mute-audio: do not even open a device.

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        std::fprintf(stderr, "SDL audio init failed: %s\n", SDL_GetError());
        return false;
    }
    m_initialized = true;

    SDL_AudioSpec want {};
    want.freq     = 44100;
    want.format   = AUDIO_F32SYS;
    want.channels = 1;
    want.samples  = 1024;
    want.callback = nullptr;

    m_device = SDL_OpenAudioDevice(nullptr, 0, &want, &m_spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!m_device)
    {
        std::fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return false;
    }

    // Queue a cushion before the device starts pulling samples. The emulator
    // delivers audio in one burst per video frame, so without a cushion the
    // queue would sit at (or below) zero and the device would periodically
    // insert silence -- audible as clicks and static.
    const Uint32 bytesPerFrame = bytesPerDeviceFrame();
    const Uint32 twoDeviceBuffers = m_spec.samples * bytesPerFrame * 2;
    const Uint32 fiftyMilliseconds =
      static_cast<Uint32>(m_spec.freq) * bytesPerFrame * 50 / 1000;
    m_prebufferBytes = std::max(twoDeviceBuffers, fiftyMilliseconds);

    // Falling below one device buffer means the next callback is likely to
    // underrun; rebuild the cushion instead of letting it crackle.
    m_lowWaterBytes = m_spec.samples * bytesPerFrame;

    m_started = false;
    SDL_PauseAudioDevice(m_device, 1);
    return true;
}

void SdlAudio::pushSamples(const float* samples, std::size_t count, int sampleRate)
{
    if (m_muted || !m_device || count == 0)
        return;

    if (!m_stream)
    {
        m_stream = SDL_NewAudioStream(AUDIO_F32SYS,
                                      1,
                                      sampleRate,
                                      AUDIO_F32SYS,
                                      1,
                                      m_spec.freq);
        if (!m_stream)
        {
            std::fprintf(stderr, "SDL_NewAudioStream failed: %s\n", SDL_GetError());
            return;
        }
    }

    if (SDL_AudioStreamPut(m_stream, samples, static_cast<int>(count * sizeof(float))) != 0)
        return;

    const Uint32 bytesPerFrame = bytesPerDeviceFrame();
    const Uint32 queued        = SDL_GetQueuedAudioSize(m_device);
    const Uint32 maxQueued     = static_cast<Uint32>(m_spec.freq) * bytesPerFrame / 8; // ~125 ms

    const int available = SDL_AudioStreamAvailable(m_stream);
    if (available <= 0)
        return;

    m_scratch.resize(static_cast<std::size_t>(available));
    const int got = SDL_AudioStreamGet(m_stream, m_scratch.data(), available);
    // If the device queue is already deep, drop this frame's converted output
    // instead of growing latency without bound.
    if (got > 0 && queued < maxQueued)
        SDL_QueueAudio(m_device, m_scratch.data(), static_cast<Uint32>(got));

    const Uint32 queuedAfter = SDL_GetQueuedAudioSize(m_device);
    if (!m_started && queuedAfter >= m_prebufferBytes)
    {
        // Playback begins only once the cushion is in place.
        SDL_PauseAudioDevice(m_device, 0);
        m_started = true;
    }
    else if (m_started && queuedAfter < m_lowWaterBytes)
    {
        // The device starved (usually a host stall). Refill before resuming
        // instead of underrunning on every callback from here on.
        SDL_PauseAudioDevice(m_device, 1);
        SDL_ClearQueuedAudio(m_device);
        m_started = false;
    }
}
} // namespace host
