#pragma once

#include "launcher/display/display.h"

#include <chrono>
#include <cstdint>
#include <vector>

namespace launcher::display
{
// Offscreen backend: no window, no SDL video. Holds one ARGB8888 canvas so
// smoke tests and CI can exercise the launcher and take screenshots, and so a
// future recorder/encoder backend can reuse the same full-frame semantics.
class HeadlessDisplay final : public Display
{
public:
    HeadlessDisplay() = default;
    ~HeadlessDisplay() override = default;

    HeadlessDisplay(const HeadlessDisplay&)            = delete;
    HeadlessDisplay& operator=(const HeadlessDisplay&) = delete;

    bool init(const Config& config, std::string& error) override;
    void shutdown() override;
    bool ready() const override;

    void updateRect(int x, int y, int w, int h,
                    const void* pixels, int stride) override;
    void present() override;
    bool readPixels(std::vector<std::uint32_t>& out,
                    int& width, int& height) override;

    bool pollEvent(Event& out) override;
    void setTextInput(bool enabled) override;

    void show() override;
    void hide() override;
    void setFullscreen(bool enabled) override;
    bool fullscreen() const override;

    std::uint32_t ticks() const override;
    void          delay(std::uint32_t ms) override;

    int canvasWidth() const override;
    int canvasHeight() const override;
    int renderScale() const override;

private:
    std::vector<std::uint32_t> m_pixels;
    int                        m_width  = 0;
    int                        m_height = 0;
    bool                       m_ready  = false;
    long long                  m_frames = 0;
    bool                       m_fullscreen = false;

    std::chrono::steady_clock::time_point m_start =
        std::chrono::steady_clock::now();
};
} // namespace launcher::display
