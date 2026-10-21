#pragma once

#include <string>

namespace launcher
{
// Per-user data directory used by the launcher, mirroring SDL_GetPrefPath's
// layout without depending on SDL:
//   Windows  %APPDATA%/AllNES/AllNES
//   macOS    ~/Library/Application Support/AllNES/AllNES
//   Linux    $XDG_DATA_HOME/AllNES/AllNES or ~/.local/share/AllNES/AllNES
// Returns an empty string when no home directory can be determined.
std::string launcherDataDir();

// <launcherDataDir()>/launcher.conf, or a relative fallback file name.
std::string defaultLauncherConfigPath();
} // namespace launcher
