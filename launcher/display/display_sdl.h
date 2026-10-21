#pragma once

#include "launcher/display/display.h"

#include <chrono>
#include <cstdint>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
union SDL_Event;

namespace launcher::display
{
// Windowed backend: one SDL window, an ARGB8888 streaming texture at canvas
// size and SDL_RenderSetLogicalSize. SDL's renderer event watch then hands us
// mouse and touch events already in canvas pixels. SDL video is initialized
// and released by this class.
class SdlDisplay final : public Display
{
public:
    SdlDisplay() = default;
    ~SdlDisplay() override;

    SdlDisplay(const SdlDisplay&)            = delete;
    SdlDisplay& operator=(const SdlDisplay&) = delete;

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
    bool translate(const SDL_Event& event, Event& out) const;
    void windowToCanvas(float wx, float wy, int& cx, int& cy) const;
    void debugInput(const char* kind, float rawX, float rawY,
                    int cx, int cy) const;
    void compose();

    SDL_Window*   m_window   = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    SDL_Texture*  m_texture  = nullptr;

    int  m_canvasWidth  = 0;
    int  m_canvasHeight = 0;
    int  m_renderScale  = 1;
    bool m_fullscreen   = false;
    bool m_videoReady   = false;

    // Touch is translated to the same normalized mouse events as a left
    // button. Multi-touch is reduced to the first finger so a second finger
    // cannot teleport the pointer or release the first one. The last canvas
    // position is cached because a finger is already gone from the touch
    // device state by the time SDL_FINGERUP can be polled.
    mutable std::int64_t m_activeFinger  = -1;
    mutable bool         m_hasFingerPos  = false;
    mutable int          m_fingerCanvasX = 0;
    mutable int          m_fingerCanvasY = 0;

    std::chrono::steady_clock::time_point m_start =
        std::chrono::steady_clock::now();
};
} // namespace launcher::display
