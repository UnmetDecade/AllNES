#include "sdl_video.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace host
{
SdlVideo::~SdlVideo()
{
    if (m_texture)
        SDL_DestroyTexture(m_texture);
    if (m_renderer)
        SDL_DestroyRenderer(m_renderer);
    if (m_window)
        SDL_DestroyWindow(m_window);
    if (m_initialized)
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

bool SdlVideo::init(int texWidth, int texHeight, int windowWidth, int windowHeight,
                    bool fullscreen, bool integerScale)
{
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
    {
        std::fprintf(stderr, "SDL video init failed: %s\n", SDL_GetError());
        return false;
    }
    m_initialized = true;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    Uint32 flags = SDL_WINDOW_RESIZABLE;
    if (fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    m_window = SDL_CreateWindow("AllNES",
                                SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED,
                                windowWidth,
                                windowHeight,
                                flags);
    if (!m_window)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
    if (!m_renderer)
    {
        // Software renderer fallback (headless CI, remote X, VMs...).
        m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!m_renderer)
    {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, texWidth, texHeight);
    if (!m_texture)
    {
        std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }

    m_fullscreen   = fullscreen;
    m_integerScale = integerScale;
    return true;
}

void SdlVideo::present(const std::uint32_t* pixels, int width, int height)
{
    if (!m_window || !m_renderer || !m_texture)
        return;

    SDL_UpdateTexture(m_texture, nullptr, pixels, width * static_cast<int>(sizeof(std::uint32_t)));

    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(m_renderer, &outW, &outH);

    // Preserve the source aspect ratio and scale to the largest size that
    // fits. Fractional scaling fills one axis exactly (the 16:9 fullscreen
    // height, for example); integer scaling keeps every NES pixel the same
    // size but letterboxes whatever does not fit in whole multiples.
    float scale = std::min(static_cast<float>(outW) / width,
                           static_cast<float>(outH) / height);
    if (m_integerScale && scale >= 1.0f)
    {
        const int whole = static_cast<int>(scale);
        if (whole >= 1 && whole * width <= outW && whole * height <= outH)
            scale = static_cast<float>(whole);
    }

    SDL_Rect dst;
    dst.w = std::min(outW, static_cast<int>(std::lround(width * scale)));
    dst.h = std::min(outH, static_cast<int>(std::lround(height * scale)));
    dst.x = (outW - dst.w) / 2;
    dst.y = (outH - dst.h) / 2;

    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);
    SDL_RenderCopy(m_renderer, m_texture, nullptr, &dst);
    SDL_RenderPresent(m_renderer);
}

void SdlVideo::setFullscreen(bool fullscreen)
{
    if (!m_window || m_fullscreen == fullscreen)
        return;

    SDL_SetWindowFullscreen(m_window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    m_fullscreen = fullscreen;
}
} // namespace host
