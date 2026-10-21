#include "launcher/display/display.h"

#include "launcher/display/display_headless.h"
#include "launcher/display/display_sdl.h"

#include <memory>

namespace launcher::display
{
std::unique_ptr<Display> createDisplay(Backend backend, std::string& error)
{
    switch (backend)
    {
    case Backend::Auto:
    case Backend::Sdl:
        return std::make_unique<SdlDisplay>();
    case Backend::Headless:
        return std::make_unique<HeadlessDisplay>();
    }

    error = "unknown display backend";
    return nullptr;
}

const char* backendName(Backend backend)
{
    switch (backend)
    {
    case Backend::Auto:
        return "auto";
    case Backend::Sdl:
        return "sdl";
    case Backend::Headless:
        return "headless";
    }
    return "unknown";
}
} // namespace launcher::display
