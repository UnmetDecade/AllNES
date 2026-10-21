#pragma once

#include <string>
#include <vector>

namespace launcher
{
struct ResolutionPreset
{
    const char* label;
    int         width;
    int         height;
    bool        fullscreen;
};

// Window size presets offered by the launcher. The emulator still applies
// integer scaling and letterboxing inside the chosen window.
const ResolutionPreset* resolutionPresets();
int                     resolutionPresetCount();

struct LauncherConfig
{
    std::string              libraryPath;
    int                      resolutionIndex = 0;
    std::vector<std::string> recent; // ROM paths, most recently launched first
};

LauncherConfig loadLauncherConfig(const std::string& path);
bool            saveLauncherConfig(const std::string& path, const LauncherConfig& config);
} // namespace launcher
