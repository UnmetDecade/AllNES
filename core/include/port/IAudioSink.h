#ifndef ALLNES_PORT_IAUDIOSINK_H
#define ALLNES_PORT_IAUDIOSINK_H

#include <cstddef>

namespace allnes
{
// Platform-independent audio output port.
//
// The APU produces mono float samples at its native rate (~894 kHz, one
// sample every two CPU clock periods). The host is responsible for resampling
// to the device rate and for buffering. Samples are delivered once per
// emulated frame.
class IAudioSink
{
public:
    virtual ~IAudioSink() = default;

    virtual void pushSamples(const float* samples, std::size_t count, int sampleRate) = 0;
};

// Discards audio -- useful for headless runs, tests and --mute-audio.
class NullAudioSink : public IAudioSink
{
public:
    void pushSamples(const float*, std::size_t, int) override {}
};

} // namespace allnes

#endif // ALLNES_PORT_IAUDIOSINK_H
