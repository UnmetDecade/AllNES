// Tests for the display abstraction's headless backend: no window is opened,
// dirty rectangles are copied into a full-canvas buffer, and the pixel format
// is ARGB8888 with opaque alpha.

#include "launcher/display/display.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    if (!condition)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    }
}

constexpr std::uint32_t kClear = 0xFF0D0E12;
} // namespace

int main()
{
    using namespace launcher::display;

    std::string error;
    std::unique_ptr<Display> display = createDisplay(Backend::Headless, error);
    check(display != nullptr, "factory returns a headless display");
    if (!display)
        return 1;

    Config config;
    config.logicalWidth  = 64;
    config.logicalHeight = 32;
    check(display->init(config, error), "headless init succeeds");

    check(display->ready(), "display reports ready after init");
    check(display->canvasWidth() == 64, "canvas width follows the logical size");
    check(display->canvasHeight() == 32, "canvas height follows the logical size");
    check(display->renderScale() == 1, "headless renders at scale 1");
    check(!display->fullscreen(), "headless starts windowed");

    // A 4x3 dirty rectangle at (10, 5), ARGB8888, stride = width * 4.
    constexpr int kRectX = 10;
    constexpr int kRectY = 5;
    constexpr int kRectW = 4;
    constexpr int kRectH = 3;
    std::vector<std::uint32_t> patch(kRectW * kRectH);
    for (int i = 0; i < kRectW * kRectH; ++i)
        patch[static_cast<std::size_t>(i)] = 0xFF000000u | static_cast<std::uint32_t>(i);

    display->updateRect(kRectX, kRectY, kRectW, kRectH,
                        patch.data(), kRectW * 4);
    display->present();

    std::vector<std::uint32_t> pixels;
    int width  = 0;
    int height = 0;
    check(display->readPixels(pixels, width, height), "readPixels succeeds");
    check(width == 64 && height == 32, "readPixels returns the canvas size");
    check(pixels.size() == 64u * 32u, "readPixels returns the whole canvas");

    bool patchMatches = true;
    for (int row = 0; row < kRectH; ++row)
    {
        for (int col = 0; col < kRectW; ++col)
        {
            const std::uint32_t expected =
                patch[static_cast<std::size_t>(row * kRectW + col)];
            const std::uint32_t actual =
                pixels[static_cast<std::size_t>((kRectY + row) * width +
                                                (kRectX + col))];
            if (actual != expected)
                patchMatches = false;
        }
    }
    check(patchMatches, "updateRect copies the dirty rectangle to its position");

    check(pixels[0] == kClear, "untouched pixels keep the clear colour");
    check(pixels[static_cast<std::size_t>((kRectY + kRectH) * width + kRectX)] == kClear,
          "pixels just below the dirty rectangle are untouched");
    check((pixels[0] >> 24) == 0xFFu, "clear colour is opaque");

    // Out-of-bounds rectangles are clipped rather than wrapping rows.
    std::vector<std::uint32_t> corner(4 * 4, 0xFFFFFFFFu);
    display->updateRect(62, 30, 4, 4, corner.data(), 4 * 4);
    check(display->readPixels(pixels, width, height), "readPixels after clipped update");
    check(pixels[static_cast<std::size_t>(31 * width + 63)] == 0xFFFFFFFFu,
          "clipped update still reaches the last pixel");
    check(pixels[static_cast<std::size_t>(29 * width + 61)] == kClear,
          "clipped update does not spill above the rectangle");

    Event event;
    check(!display->pollEvent(event), "headless backend produces no events");

    display->setTextInput(true);
    display->show();
    display->hide();
    display->setFullscreen(true);
    check(display->fullscreen(), "fullscreen flag is tracked");

    display->shutdown();
    check(!display->ready(), "shutdown clears ready");
    display->shutdown(); // idempotent

    if (g_failures == 0)
        std::printf("display headless tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
