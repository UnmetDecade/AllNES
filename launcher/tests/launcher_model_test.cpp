// Tests for the launcher's SDL-free model: ROM probing/scanning, directory
// listing, path helpers and the settings file.

#include "library.h"
#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using namespace launcher;

namespace
{
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    if (!condition)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    }
}

class TempDir
{
public:
    TempDir()
    {
        std::error_code ec;
        m_path = fs::temp_directory_path(ec) / "allnes_launcher_model_test";
        fs::remove_all(m_path, ec);
        fs::create_directories(m_path, ec);
    }

    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }

    const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
};

void writeFile(const fs::path& path, const std::string& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeRom(const fs::path& path,
              int mapper,
              bool pal     = false,
              bool trainer = false,
              unsigned char prgBanks = 1)
{
    unsigned char header[16] = { 'N', 'E', 'S', 0x1A, prgBanks, 0, 0, 0,
                                 0, 0, 0, 0, 0, 0, 0, 0 };
    header[6] = static_cast<unsigned char>(((mapper & 0x0F) << 4) | (trainer ? 0x04 : 0));
    header[7] = static_cast<unsigned char>(mapper & 0xF0);
    header[10] = pal ? 1 : 0;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    file.write("PRG", 3);
}

void testIsNesPath()
{
    check(isNesPath("game.nes"), "lower-case .nes");
    check(isNesPath("GAME.NES"), "upper-case .NES");
    check(isNesPath("/roms/Game.Nes"), "mixed-case .Nes");
    check(!isNesPath("game.txt"), "non-NES extension");
    check(!isNesPath("game.nes.bak"), "compound extension");
    check(!isNesPath("nes"), "no extension");
}

void testProbeRom(const TempDir& temp)
{
    const fs::path nrom = temp.path() / "valid.nes";
    writeRom(nrom, 0);
    const RomEntry valid = probeRom(nrom.string());
    check(valid.mapper == 0, "NROM mapper is 0");
    check(valid.supported, "NROM is supported");
    check(valid.name == "valid.nes", "probe keeps the file name");
    check(valid.title == "valid", "probe strips the extension for the title");
    check(valid.sizeBytes == 19, "probe records the file size");

    const fs::path mmc3 = temp.path() / "mmc3.nes";
    writeRom(mmc3, 4);
    const RomEntry mapper4 = probeRom(mmc3.string());
    check(mapper4.mapper == 4 && mapper4.supported, "mapper 4 is supported");

    const fs::path gxrom = temp.path() / "gxrom.nes";
    writeRom(gxrom, 66);
    const RomEntry mapper66 = probeRom(gxrom.string());
    check(mapper66.mapper == 66 && mapper66.supported, "mapper 66 is supported");

    const fs::path unsupported = temp.path() / "m5.nes";
    writeRom(unsupported, 5);
    const RomEntry mapper5 = probeRom(unsupported.string());
    check(mapper5.mapper == 5 && !mapper5.supported, "mapper 5 is rejected");

    const fs::path pal = temp.path() / "pal.nes";
    writeRom(pal, 0, true);
    check(!probeRom(pal.string()).supported, "PAL image is rejected");

    const fs::path trainer = temp.path() / "trainer.nes";
    writeRom(trainer, 0, false, true);
    check(!probeRom(trainer.string()).supported, "trainer image is rejected");

    const fs::path badMagic = temp.path() / "bad.nes";
    writeFile(badMagic, "NOT A NES FILE....");
    const RomEntry invalid = probeRom(badMagic.string());
    check(invalid.mapper == -1, "bad magic leaves mapper at -1");
    check(!invalid.supported, "bad magic is unsupported");

    const fs::path missing = temp.path() / "missing.nes";
    const RomEntry absent = probeRom(missing.string());
    check(absent.mapper == -1 && !absent.supported, "missing file is unsupported");
}

void testScanLibrary(const TempDir& temp)
{
    const fs::path library = temp.path() / "library";
    std::error_code ec;
    fs::create_directories(library, ec);

    writeRom(library / "b.NES", 1);
    writeRom(library / "A.nes", 2);
    writeRom(library / ".hidden.nes", 0);
    writeFile(library / "notes.txt", "not a rom");
    fs::create_directories(library / "sub.nes", ec);
    fs::create_directories(library / "images", ec);
    writeFile(library / "images" / "A.png", "not really a png");

    std::string error = "sentinel";
    const std::vector<RomEntry> roms = scanLibrary(library.string(), error);

    check(error.empty(), "scan reports no error");
    check(roms.size() == 2, "scan finds exactly two ROMs");
    if (roms.size() == 2)
    {
        check(roms[0].name == "A.nes", "case-insensitive sort: A before b");
        check(roms[0].mapper == 2, "A.nes is mapper 2");
        check(!roms[0].coverPath.empty(), "scan finds a cover next to the library");
        check(roms[1].name == "b.NES", "case-insensitive sort keeps b.NES");
        check(roms[1].mapper == 1, "b.NES is mapper 1");
        check(roms[1].coverPath.empty(), "missing cover stays empty");
    }

    std::string missingError;
    const std::vector<RomEntry> missing = scanLibrary((temp.path() / "nope").string(), missingError);
    check(missing.empty(), "missing folder yields no ROMs");
    check(!missingError.empty(), "missing folder yields an error");
}

void testPathHelpers(const TempDir& temp)
{
#ifndef _WIN32
    const char* oldHome = std::getenv("HOME");
    const std::string savedHome = oldHome ? oldHome : "";
    setenv("HOME", temp.path().c_str(), 1);
    check(expandUserPath("~/roms") == temp.path().string() + "/roms", "expands ~/");
    check(expandUserPath("~") == temp.path().string(), "expands bare ~");
    if (oldHome)
        setenv("HOME", savedHome.c_str(), 1);
    else
        unsetenv("HOME");
    check(expandUserPath("plain") == "plain", "leaves plain paths alone");
#endif
}

void testSettings(const TempDir& temp)
{
    check(resolutionPresetCount() == 6, "six resolution presets");
    check(std::string(resolutionPresets()[0].label) == "3x 768x720", "default preset label");
    check(resolutionPresets()[5].fullscreen, "last preset is fullscreen");

    const LauncherConfig defaults = loadLauncherConfig((temp.path() / "absent.conf").string());
    check(defaults.libraryPath.empty(), "missing config has no library");
    check(defaults.resolutionIndex == 0, "missing config defaults to preset 0");

    LauncherConfig config;
    config.libraryPath     = "/home/player/ROMs";
    config.resolutionIndex = 3;
    config.recent          = { "/home/player/ROMs/contra.nes", "/home/player/ROMs/mm2.nes" };

    const fs::path path = temp.path() / "nested" / "launcher.conf";
    check(saveLauncherConfig(path.string(), config), "config saves (creating parent dirs)");

    const LauncherConfig loaded = loadLauncherConfig(path.string());
    check(loaded.libraryPath == config.libraryPath, "library path round-trips");
    check(loaded.resolutionIndex == config.resolutionIndex, "resolution index round-trips");
    check(loaded.recent.size() == 2 && loaded.recent[0] == config.recent[0],
          "recent games round-trip in order");

    const fs::path high = temp.path() / "high.conf";
    writeFile(high, "resolution=99\n");
    check(loadLauncherConfig(high.string()).resolutionIndex == 5, "high index is clamped");

    const fs::path low = temp.path() / "low.conf";
    writeFile(low, "resolution=-3\n");
    check(loadLauncherConfig(low.string()).resolutionIndex == 0, "negative index is clamped");

    const fs::path comments = temp.path() / "comments.conf";
    writeFile(comments, "# comment\n; another\nlibrary = /roms \nresolution = 2\n");
    const LauncherConfig parsed = loadLauncherConfig(comments.string());
    check(parsed.libraryPath == "/roms", "values are trimmed");
    check(parsed.resolutionIndex == 2, "resolution value is parsed");
}
} // namespace

int main()
{
    TempDir temp;

    testIsNesPath();
    testProbeRom(temp);
    testScanLibrary(temp);
    testPathHelpers(temp);
    testSettings(temp);

    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d launcher model check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All launcher model checks passed.\n");
    return 0;
}
