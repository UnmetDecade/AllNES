#ifndef ALLNES_PORT_IVIDEOSINK_H
#define ALLNES_PORT_IVIDEOSINK_H

#include <cstdint>

namespace allnes
{
// Platform-independent video output port.
//
// The core renders a complete 256x240 frame and hands it to the host once per
// emulated frame. Pixels are row-major 0xAARRGGBB, so a host can map them
// directly onto SDL_PIXELFORMAT_ARGB8888 (or convert trivially elsewhere).
class IVideoSink
{
public:
    virtual ~IVideoSink() = default;

    virtual void present(const std::uint32_t* pixels, int width, int height) = 0;
};

// Discards frames -- useful for headless runs and tests.
class NullVideoSink : public IVideoSink
{
public:
    void present(const std::uint32_t*, int, int) override {}
};

} // namespace allnes

#endif // ALLNES_PORT_IVIDEOSINK_H
