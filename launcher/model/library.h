#pragma once

#include <string>
#include <vector>

namespace launcher
{
// One ROM discovered in the game library. The header is probed cheaply so the
// launcher can show the mapper and reject unsupported images before launch.
struct RomEntry
{
    std::string path;
    std::string name;      // file name including the extension
    std::string title;     // file name without the extension
    std::string coverPath; // <library>/images/<title>.png or .jpg, empty if absent
    long long   sizeBytes = 0;
    int         mapper    = -1;    // -1 when the file is not a valid iNES image
    bool        supported = false;
};

// True for names ending in ".nes" (case-insensitive).
bool isNesPath(const std::string& path);

// Reads the 16-byte iNES header and fills mapper/supported. Invalid headers
// leave mapper at -1 and supported false.
RomEntry probeRom(const std::string& path);

// Lists .NES files in `dir`, probes each header and sorts case-insensitively
// by file name. When `recursive` is set the scan descends into subdirectories.
// On failure `error` is set and the result is empty.
std::vector<RomEntry> scanLibrary(const std::string& dir, std::string& error,
                                  bool recursive = false);

// Expands a leading "~" using $HOME / %USERPROFILE%.
std::string expandUserPath(const std::string& path);
} // namespace launcher
