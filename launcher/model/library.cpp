#include "library.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace launcher
{
namespace
{
namespace fs = std::filesystem;

bool supportedMapper(int mapper)
{
    switch (mapper)
    {
    case 0:  // NROM
    case 1:  // MMC1
    case 2:  // UxROM
    case 3:  // CNROM
    case 4:  // MMC3
    case 7:  // AxROM
    case 11: // ColorDreams
    case 66: // GxROM
    case 245: // MMC3-based bootleg board (Dragon Quest ports)
        return true;
    default:
        return false;
    }
}

unsigned char lowerAscii(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c - 'A' + 'a') : c;
}

bool nameLess(const std::string& a, const std::string& b)
{
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i)
    {
        const unsigned char ca = lowerAscii(static_cast<unsigned char>(a[i]));
        const unsigned char cb = lowerAscii(static_cast<unsigned char>(b[i]));
        if (ca != cb)
            return ca < cb;
    }
    if (a.size() != b.size())
        return a.size() < b.size();
    return a < b;
}

std::string homeDirectory()
{
    const char* home = std::getenv("HOME");
    if (!home)
        home = std::getenv("USERPROFILE");
    return home ? std::string(home) : std::string();
}
} // namespace

bool isNesPath(const std::string& path)
{
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(lowerAscii(c)); });
    return ext == ".nes";
}

RomEntry probeRom(const std::string& path)
{
    RomEntry entry;
    const fs::path filePath(path);
    entry.path  = path;
    entry.name  = filePath.filename().string();
    entry.title = filePath.stem().string();

    std::error_code sizeEc;
    const auto size = fs::file_size(filePath, sizeEc);
    if (!sizeEc)
        entry.sizeBytes = static_cast<long long>(size);

    std::ifstream file(path, std::ios::binary);
    if (!file)
        return entry;

    unsigned char header[16] = {};
    if (!file.read(reinterpret_cast<char*>(header), sizeof(header)))
        return entry;

    if (header[0] != 'N' || header[1] != 'E' || header[2] != 'S' || header[3] != 0x1A)
        return entry;

    const int prgBanks = header[4];
    const bool trainer = (header[6] & 0x04) != 0;
    const bool pal     = (header[10] & 0x03) == 1;
    const int mapper   = ((header[6] >> 4) & 0x0F) | (header[7] & 0xF0);

    entry.mapper    = mapper;
    entry.supported = prgBanks > 0 && !trainer && !pal && supportedMapper(mapper);
    return entry;
}

std::vector<RomEntry> scanLibrary(const std::string& dir, std::string& error)
{
    std::vector<RomEntry> roms;
    error.clear();

    std::error_code ec;
    if (!fs::is_directory(dir, ec))
    {
        error = "Not a directory: " + dir;
        return roms;
    }

    fs::directory_iterator it(dir, ec);
    if (ec)
    {
        error = "Cannot read " + dir + ": " + ec.message();
        return roms;
    }

    // Box art is kept in a sibling "images" folder, named after the ROM stem.
    const fs::path imageDir = fs::path(dir) / "images";
    const char* const kCoverExtensions[] = { ".png", ".jpg", ".jpeg" };

    const fs::directory_iterator end;
    while (it != end)
    {
        const fs::directory_entry& file = *it;
        const std::string name = file.path().filename().string();

        if (!name.empty() && name[0] != '.')
        {
            std::error_code entryEc;
            if (file.is_regular_file(entryEc) && isNesPath(name))
            {
                RomEntry entry = probeRom(file.path().string());
                for (const char* extension : kCoverExtensions)
                {
                    std::error_code coverEc;
                    const fs::path candidate = imageDir / (entry.title + extension);
                    if (fs::is_regular_file(candidate, coverEc))
                    {
                        entry.coverPath = candidate.string();
                        break;
                    }
                }
                roms.push_back(std::move(entry));
            }
        }

        it.increment(ec);
        if (ec)
        {
            ec.clear(); // skip an unreadable entry and keep scanning
            if (it == end)
                break;
        }
    }

    std::sort(roms.begin(), roms.end(),
              [](const RomEntry& a, const RomEntry& b) { return nameLess(a.name, b.name); });
    return roms;
}

std::string expandUserPath(const std::string& path)
{
    if (path.empty() || path[0] != '~')
        return path;

    const std::string home = homeDirectory();
    if (home.empty())
        return path;

    if (path.size() == 1)
        return home;
    if (path[1] == '/' || path[1] == '\\')
        return home + path.substr(1);
    return path;
}
} // namespace launcher
