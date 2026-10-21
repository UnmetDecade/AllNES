#pragma once

#include "launcher/display/display.h"
#include "launcher/types.h"

#include <memory>
#include <string>

namespace launcher
{
// LVGL-backed launcher (the only launcher UI). It builds the widget tree and
// owns the application state; windowing, frame submission, input translation,
// fullscreen and timing are delegated to the display layer below it.
//
// The UI never includes SDL: it creates an LVGL display at the canvas size
// reported by the backend and scales every layout metric by renderScale, so
// HiDPI windows get crisp text and artwork.
class LvglLauncherApp
{
public:
    LvglLauncherApp();
    ~LvglLauncherApp();

    LvglLauncherApp(const LvglLauncherApp&)            = delete;
    LvglLauncherApp& operator=(const LvglLauncherApp&) = delete;

    bool init(const std::string& libraryOverride, long menuFrames,
              display::Backend backend = display::Backend::Auto);
    bool run(LaunchRequest& out);

    void hideWindow();
    void showWindow();
    void setError(const std::string& message);
    void rescan();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace launcher
