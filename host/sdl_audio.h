#ifndef ALLNES_HOST_SDL_AUDIO_H
#define ALLNES_HOST_SDL_AUDIO_H

#include "port/IAudioSink.h"

#include <SDL.h>
#include <cstdint>
#include <vector>

namespace host
{
// Receives the APU's native-rate mono stream, resamples it to the device rate
// with SDL_AudioStream and feeds SDL's audio queue.
class SdlAudio : public allnes::IAudioSink
{
public:
    SdlAudio() = default;
    ~SdlAudio() override;

    bool init(bool muted);
    void pushSamples(const float* samples, std::size_t count, int sampleRate) override;

    // Unmuting after starting with --mute-audio opens the device lazily.
    void setMuted(bool muted);
    bool muted() const { return m_muted; }

private:
    SDL_AudioDeviceID    m_device   = 0;
    SDL_AudioStream*     m_stream   = nullptr;
    SDL_AudioSpec        m_spec {};
    bool                 m_muted       = false;
    bool                 m_initialized = false;
    std::vector<std::uint8_t> m_scratch;
};
} // namespace host

#endif // ALLNES_HOST_SDL_AUDIO_H
