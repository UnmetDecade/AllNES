#include "settings.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace launcher
{
namespace
{
namespace fs = std::filesystem;

const ResolutionPreset kPresets[] = {
    { "3x 768x720",   768,  720,  false },
    { "4x 1024x960",  1024, 960,  false },
    { "5x 1280x1200", 1280, 1200, false },
    { "1920x1080",    1920, 1080, false },
    { "2560x1440",    2560, 1440, false },
    { "全屏",         0,    0,    true  },
};

constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

std::string trim(const std::string& text)
{
    std::size_t begin = 0;
    std::size_t end   = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return text.substr(begin, end - begin);
}

bool parseBool(const std::string& text)
{
    return text == "1" || text == "true" || text == "on" || text == "yes";
}

int clampInt(int value, int lo, int hi)
{
    return std::max(lo, std::min(hi, value));
}
} // namespace

const ResolutionPreset* resolutionPresets()
{
    return kPresets;
}

int resolutionPresetCount()
{
    return kPresetCount;
}

LauncherConfig loadLauncherConfig(const std::string& path)
{
    LauncherConfig config;

    std::ifstream file(path);
    if (!file)
        return config;

    std::string line;
    while (std::getline(file, line))
    {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        const std::size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;

        const std::string key   = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));

        if (key == "library")
            config.libraryPath = value;
        else if (key == "resolution")
            config.resolutionIndex = std::atoi(value.c_str());
        else if (key == "recent" && !value.empty())
        {
            const auto duplicate = std::find(config.recent.begin(), config.recent.end(), value);
            if (duplicate == config.recent.end())
                config.recent.push_back(value);
        }
        else if (key == "favorite" && !value.empty())
        {
            const auto duplicate = std::find(config.favorites.begin(), config.favorites.end(), value);
            if (duplicate == config.favorites.end())
                config.favorites.push_back(value);
        }
        else if (key == "recursive")
            config.recursiveScan = parseBool(value);
        else if (key == "patches")
            config.detectPatches = parseBool(value);
        else if (key == "pixelfilter")
            config.pixelFilter = std::atoi(value.c_str());
        else if (key == "aspect")
            config.keepAspect = parseBool(value);
        else if (key == "integerscale")
            config.integerScale = parseBool(value);
        else if (key == "vsync")
            config.vsync = parseBool(value);
        else if (key == "covers")
            config.showCovers = parseBool(value);
        else if (key == "coverstyle")
            config.coverStyle = std::atoi(value.c_str());
        else if (key == "stickdpad")
            config.stickToDpad = parseBool(value);
        else if (key == "deadzone")
            config.deadzone = std::strtof(value.c_str(), nullptr);
        else if (key == "turbo")
            config.turbo = std::atoi(value.c_str());
    }

    if (config.recent.size() > 12)
        config.recent.resize(12);

    if (config.resolutionIndex < 0)
        config.resolutionIndex = 0;
    if (config.resolutionIndex >= kPresetCount)
        config.resolutionIndex = kPresetCount - 1;

    config.pixelFilter = clampInt(config.pixelFilter, 0, 3);
    config.coverStyle  = clampInt(config.coverStyle, 0, 2);
    config.deadzone    = std::max(0.0f, std::min(0.5f, config.deadzone));
    config.turbo       = clampInt(config.turbo, 1, 30);
    return config;
}

bool saveLauncherConfig(const std::string& path, const LauncherConfig& config)
{
    std::error_code ec;
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty())
        fs::create_directories(parent, ec);

    std::ofstream file(path, std::ios::trunc);
    if (!file)
        return false;

    file << "# AllNES launcher settings\n";
    file << "library=" << config.libraryPath << '\n';
    file << "resolution=" << config.resolutionIndex << '\n';
    file << "recursive=" << (config.recursiveScan ? "1" : "0") << '\n';
    file << "patches=" << (config.detectPatches ? "1" : "0") << '\n';
    file << "pixelfilter=" << config.pixelFilter << '\n';
    file << "aspect=" << (config.keepAspect ? "1" : "0") << '\n';
    file << "integerscale=" << (config.integerScale ? "1" : "0") << '\n';
    file << "vsync=" << (config.vsync ? "1" : "0") << '\n';
    file << "covers=" << (config.showCovers ? "1" : "0") << '\n';
    file << "coverstyle=" << config.coverStyle << '\n';
    file << "stickdpad=" << (config.stickToDpad ? "1" : "0") << '\n';
    file << "deadzone=" << config.deadzone << '\n';
    file << "turbo=" << config.turbo << '\n';
    for (const std::string& entry : config.recent)
        file << "recent=" << entry << '\n';
    for (const std::string& entry : config.favorites)
        file << "favorite=" << entry << '\n';
    return static_cast<bool>(file);
}
} // namespace launcher
