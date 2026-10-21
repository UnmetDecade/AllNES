#include "paths.h"

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace launcher
{
namespace fs = std::filesystem;

namespace
{
fs::path homeDirectory()
{
#if defined(_WIN32)
    if (const char* home = std::getenv("USERPROFILE"))
    {
        if (*home)
            return fs::path(home);
    }
#else
    if (const char* home = std::getenv("HOME"))
    {
        if (*home)
            return fs::path(home);
    }
#endif
    return {};
}
} // namespace

std::string launcherDataDir()
{
    fs::path dir;

#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"))
    {
        if (*appdata)
            dir = fs::path(appdata) / "AllNES" / "AllNES";
    }
    if (dir.empty())
    {
        const fs::path home = homeDirectory();
        if (!home.empty())
            dir = home / "AppData" / "Roaming" / "AllNES" / "AllNES";
    }
#elif defined(__APPLE__)
    const fs::path home = homeDirectory();
    if (!home.empty())
        dir = home / "Library" / "Application Support" / "AllNES" / "AllNES";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"))
    {
        if (*xdg)
            dir = fs::path(xdg) / "AllNES" / "AllNES";
    }
    if (dir.empty())
    {
        const fs::path home = homeDirectory();
        if (!home.empty())
            dir = home / ".local" / "share" / "AllNES" / "AllNES";
    }
#endif

    if (dir.empty())
        return {};
    return dir.string();
}

std::string defaultLauncherConfigPath()
{
    const std::string dir = launcherDataDir();
    if (dir.empty())
        return "allnes_launcher.conf";
    return (fs::path(dir) / "launcher.conf").string();
}
} // namespace launcher
