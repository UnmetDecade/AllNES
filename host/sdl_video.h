#ifndef ALLNES_HOST_SDL_VIDEO_H
#define ALLNES_HOST_SDL_VIDEO_H

#include "port/IVideoSink.h"

#include <SDL.h>
#include <cstdint>

namespace host
{
class SdlVideo : public allnes::IVideoSink
{
public:
    SdlVideo() = default;
    ~SdlVideo() override;

    // texWidth/texHeight is the NES frame size; windowWidth/windowHeight is
    // the initial desktop window size. integerScale keeps every source pixel
    // the same on-screen size and letterboxes the remainder; when false the
    // image is scaled fractionally to fill one axis of the window.
    bool init(int texWidth, int texHeight, int windowWidth, int windowHeight,
              bool fullscreen, bool integerScale = false);
    void present(const std::uint32_t* pixels, int width, int height) override;

    void setFullscreen(bool fullscreen);
    bool isFullscreen() const { return m_fullscreen; }

    // Used to attach modal dialogs (e.g. "leave game?") to the game window.
    SDL_Window* window() const { return m_window; }

private:
    SDL_Window*   m_window       = nullptr;
    SDL_Renderer* m_renderer     = nullptr;
    SDL_Texture*  m_texture      = nullptr;
    bool          m_fullscreen   = false;
    bool          m_integerScale = false;
    bool          m_initialized  = false;
};
} // namespace host

#endif // ALLNES_HOST_SDL_VIDEO_H
