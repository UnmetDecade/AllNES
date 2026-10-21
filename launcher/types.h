#pragma once

#include <string>

namespace launcher
{
// What the launcher hands back to the host when the player starts a game.
struct LaunchRequest
{
    std::string romPath;
    int         width      = 768;
    int         height     = 720;
    bool        fullscreen = false;
};
} // namespace launcher
