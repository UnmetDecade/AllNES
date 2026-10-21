#include "launcher/display/display_headless.h"

#include <algorithm>
#include <cstring>
#include <thread>

namespace launcher::display
{
namespace
{
constexpr std::uint32_t kClearColor = 0xFF0D0E12; // opaque, matches the SDL backend
} // namespace

bool HeadlessDisplay::init(const Config& config, std::string& error)
{
    (void)error;

    m_width     = config.logicalWidth  > 0 ? config.logicalWidth  : 1920;
    m_height    = config.logicalHeight > 0 ? config.logicalHeight : 1080;
    m_pixels.assign(static_cast<std::size_t>(m_width) *
                        static_cast<std::size_t>(m_height),
                    kClearColor);
    m_ready    = true;
    m_frames   = 0;
    m_start    = std::chrono::steady_clock::now();
    m_fullscreen = false;
    return true;
}

void HeadlessDisplay::shutdown()
{
    m_pixels.clear();
    m_pixels.shrink_to_fit();
    m_width  = 0;
    m_height = 0;
    m_ready  = false;
}

bool HeadlessDisplay::ready() const
{
    return m_ready;
}

void HeadlessDisplay::updateRect(int x, int y, int w, int h,
                                 const void* pixels, int stride)
{
    if (!m_ready || !pixels || w <= 0 || h <= 0)
        return;

    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(m_width, x + w);
    const int y1 = std::min(m_height, y + h);
    if (x1 <= x0 || y1 <= y0)
        return;

    const auto* source = static_cast<const std::uint8_t*>(pixels);
    source += static_cast<std::size_t>(y0 - y) * static_cast<std::size_t>(stride) +
              static_cast<std::size_t>(x0 - x) * 4u;

    const std::size_t copyBytes =
        static_cast<std::size_t>(x1 - x0) * sizeof(std::uint32_t);
    for (int row = y0; row < y1; ++row)
    {
        std::uint8_t* destination =
            reinterpret_cast<std::uint8_t*>(m_pixels.data()) +
            static_cast<std::size_t>(row) * static_cast<std::size_t>(m_width) * 4u +
            static_cast<std::size_t>(x0) * 4u;
        std::memcpy(destination, source, copyBytes);
        source += stride;
    }
}

void HeadlessDisplay::present()
{
    if (m_ready)
        ++m_frames;
}

bool HeadlessDisplay::readPixels(std::vector<std::uint32_t>& out,
                                 int& width, int& height)
{
    if (!m_ready)
        return false;

    out    = m_pixels;
    width  = m_width;
    height = m_height;
    return true;
}

bool HeadlessDisplay::pollEvent(Event& out)
{
    out = Event();
    return false;
}

void HeadlessDisplay::setTextInput(bool enabled)
{
    (void)enabled;
}

void HeadlessDisplay::show()
{
}

void HeadlessDisplay::hide()
{
}

void HeadlessDisplay::setFullscreen(bool enabled)
{
    m_fullscreen = enabled;
}

bool HeadlessDisplay::fullscreen() const
{
    return m_fullscreen;
}

std::uint32_t HeadlessDisplay::ticks() const
{
    const auto elapsed = std::chrono::steady_clock::now() - m_start;
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

void HeadlessDisplay::delay(std::uint32_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int HeadlessDisplay::canvasWidth() const
{
    return m_width;
}

int HeadlessDisplay::canvasHeight() const
{
    return m_height;
}

float HeadlessDisplay::renderScale() const
{
    return 1.0f;
}

int HeadlessDisplay::padCount() const
{
    return 0;
}

bool HeadlessDisplay::padButton(int, int) const
{
    return false;
}

int HeadlessDisplay::padAxis(int, int) const
{
    return 0;
}

std::string HeadlessDisplay::padName(int) const
{
    return std::string();
}

bool HeadlessDisplay::padRumble(int)
{
    return false;
}
} // namespace launcher::display
