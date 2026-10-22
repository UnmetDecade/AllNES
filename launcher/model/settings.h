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
    // Directory the folder dialog was last left in. Used to reopen the picker
    // where the user left off, including after cancelling.
    std::string              lastPickerDir;
    int                      resolutionIndex = 0;
    std::vector<std::string> recent;     // ROM paths, most recently launched first
    std::vector<std::string> favorites;  // ROM paths marked as favorites

    // Library scanning.
    bool recursiveScan = false;          // scan subdirectories too
    bool detectPatches = false;          // recognize .ips/.bps alongside a ROM

    // Display / emulator picture.
    int  pixelFilter  = 1;               // 0 CRT, 1 nearest, 2 bilinear, 3 LCD
    bool keepAspect   = true;            // 4:3
    bool integerScale = true;            // whole-pixel scaling
    bool vsync        = true;            // synchronize to vertical refresh

    // Covers.
    bool showCovers = true;              // false: grid shows titles only
    int  coverStyle = 0;                 // 0 mosaic, 1 solid+initial, 2 gray

    // Gamepad.
    bool  stickToDpad = true;            // left stick emulates the D-pad
    float deadzone    = 0.15f;           // stick dead zone (0..0.5)
    int   turbo       = 12;              // turbo rate in frames
};

LauncherConfig loadLauncherConfig(const std::string& path);
bool            saveLauncherConfig(const std::string& path, const LauncherConfig& config);
} // namespace launcher
