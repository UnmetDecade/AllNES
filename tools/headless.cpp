// Headless runner: no SDL, no window, no audio device. Useful for CI and for
// checking that the core layers stay free of platform dependencies.
//
//   allnes_headless --rom game.nes --frames 120 --hash
//   allnes_headless --rom test.nes --frames 2 --check-stripes --repeat 2
//   allnes_headless --rom game.nes --frames 1 --dump-ppm frame.ppm

#include "Emulator.h"
#include "Log.h"
#include "port/IAudioSink.h"
#include "port/IInputSource.h"
#include "port/IVideoSink.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
struct NullVideo : allnes::IVideoSink
{
    void present(const std::uint32_t*, int, int) override {}
};

// Holds buttons on the first controller for a few frames starting at a given
// frame. Games that boot into a title screen wait for Start, so scripted input
// is needed to reach gameplay in headless runs.
struct ScriptedInput : allnes::IInputSource
{
    struct Press
    {
        int          frame;
        std::uint8_t button;
    };

    std::vector<Press> presses;
    int                frame = 0;

    std::uint8_t buttonState(int player) const override
    {
        if (player != 0)
            return 0;

        std::uint8_t state = 0;
        for (const auto& press : presses)
        {
            if (frame >= press.frame && frame < press.frame + 2)
                state |= press.button;
        }
        return state;
    }
};

bool buttonFromName(const std::string& name, std::uint8_t& out)
{
    if (name == "a")
        out = allnes::ButtonA;
    else if (name == "b")
        out = allnes::ButtonB;
    else if (name == "select")
        out = allnes::ButtonSelect;
    else if (name == "start")
        out = allnes::ButtonStart;
    else if (name == "up")
        out = allnes::ButtonUp;
    else if (name == "down")
        out = allnes::ButtonDown;
    else if (name == "left")
        out = allnes::ButtonLeft;
    else if (name == "right")
        out = allnes::ButtonRight;
    else
        return false;
    return true;
}

struct CountingAudio : allnes::IAudioSink
{
    std::size_t   totalSamples = 0;
    int           lastRate     = 0;
    std::uint64_t hash         = 1469598103934665603ull;

    void pushSamples(const float* samples, std::size_t count, int sampleRate) override
    {
        totalSamples += count;
        lastRate      = sampleRate;

        const auto* bytes = reinterpret_cast<const unsigned char*>(samples);
        for (std::size_t i = 0; i < count * sizeof(float); ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    }
};

std::uint64_t fnv1a(const std::uint32_t* data, std::size_t count)
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto*   bytes = reinterpret_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < count * sizeof(std::uint32_t); ++i)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

bool dumpPpm(const char* path, const std::uint32_t* pixels, int width, int height)
{
    std::FILE* f = std::fopen(path, "wb");
    if (!f)
        return false;

    std::fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (int i = 0; i < width * height; ++i)
    {
        const std::uint32_t p = pixels[i];
        const unsigned char rgb[3] = {
            static_cast<unsigned char>((p >> 16) & 0xFF),
            static_cast<unsigned char>((p >> 8) & 0xFF),
            static_cast<unsigned char>(p & 0xFF),
        };
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
    return true;
}

// The generated test ROM draws 8-pixel vertical stripes: even tile columns use
// palette colour 0 (black), odd columns colour 1 (white).
bool checkStripes(const std::uint32_t* pixels, int width, int height)
{
    const int y = height / 2;
    const std::uint32_t a  = pixels[y * width + 0];
    const std::uint32_t b  = pixels[y * width + 8];
    const std::uint32_t a2 = pixels[y * width + 16];
    const std::uint32_t b2 = pixels[y * width + 24];
    const std::uint32_t top = pixels[8 * width + 8];
    const std::uint32_t bot = pixels[(height - 8) * width + 8];

    const bool evenUniform = (a == a2);
    const bool oddUniform  = (b == b2);
    const bool alternating = (a != b);
    const bool vertical    = (b == top && b == bot);

    std::printf("stripes: even=%s odd=%s alternating=%s vertical=%s\n",
                evenUniform ? "yes" : "no",
                oddUniform ? "yes" : "no",
                alternating ? "yes" : "no",
                vertical ? "yes" : "no");

    return evenUniform && oddUniform && alternating && vertical;
}

void usage(const char* argv0)
{
    std::printf(
      "Usage: %s --rom <file.nes> [options]\n\n"
      "  --frames N          frames to emulate (default 60)\n"
      "  --repeat N          reset and re-run the frame sequence N times\n"
      "  --hash              print a frame hash (FNV-1a 64)\n"
      "  --expect-hash H     fail unless the last frame hash matches\n"
      "  --check-stripes     verify the bundled test-ROM pattern\n"
      "  --dump-ppm FILE     write the last frame as a PPM image\n"
      "  --press BTN@FRAME   hold BTN (a/b/select/start/up/down/left/right)\n"
      "                      for two frames; may be given multiple times\n"
      "  --pc                print the CPU program counter of the last frame\n"
      "  --cpu-trace         log every executed instruction to stderr\n"
      "  --quiet             only print the final summary\n",
      argv0);
}
} // namespace

int main(int argc, char* argv[])
{
    std::string romPath;
    std::string expectHash;
    std::string dumpPath;
    long        frames     = 60;
    int         repeats    = 1;
    bool        printHash  = false;
    bool        check      = false;
    bool        quiet      = false;
    bool        printPC    = false;
    bool        cpuTrace   = false;
    ScriptedInput input;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto value = [&](const char* name) -> const char* {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "%s: missing value for %s\n", argv[0], name);
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--rom")
            romPath = value("--rom");
        else if (arg == "--frames")
            frames = std::atol(value("--frames"));
        else if (arg == "--repeat")
            repeats = std::atoi(value("--repeat"));
        else if (arg == "--hash")
            printHash = true;
        else if (arg == "--expect-hash")
            expectHash = value("--expect-hash");
        else if (arg == "--check-stripes")
            check = true;
        else if (arg == "--dump-ppm")
            dumpPath = value("--dump-ppm");
        else if (arg == "--press")
        {
            const std::string spec = value("--press");
            const auto        at   = spec.find('@');
            std::uint8_t      button = 0;
            if (at == std::string::npos || !buttonFromName(spec.substr(0, at), button))
            {
                std::fprintf(stderr, "%s: bad --press spec '%s' (expected BTN@FRAME)\n", argv[0],
                             spec.c_str());
                return 2;
            }
            input.presses.push_back({ std::atoi(spec.c_str() + at + 1), button });
        }
        else if (arg == "--pc")
            printPC = true;
        else if (arg == "--cpu-trace")
            cpuTrace = true;
        else if (arg == "--quiet")
            quiet = true;
        else if (arg == "-h" || arg == "--help")
        {
            usage(argv[0]);
            return 0;
        }
        else
        {
            std::fprintf(stderr, "%s: unknown argument '%s'\n", argv[0], arg.c_str());
            return 2;
        }
    }

    if (romPath.empty())
    {
        usage(argv[0]);
        return 2;
    }
    if (frames < 1)
        frames = 1;
    if (repeats < 1)
        repeats = 1;

    allnes::Log::get().setLogStream(std::cerr);
    allnes::Log::get().setLevel(cpuTrace ? allnes::CpuTrace : allnes::Error);

    NullVideo        video;
    CountingAudio    audio;
    allnes::Emulator emulator(video, audio, input);

    if (!emulator.loadRom(romPath))
    {
        std::fprintf(stderr, "Could not load ROM '%s'\n", romPath.c_str());
        return 1;
    }

    const allnes::Cartridge& cart = emulator.cartridge();
    if (!quiet)
    {
        std::printf("rom: %s\n", romPath.c_str());
        std::printf("mapper: %d, prg: %zu KiB, chr: %zu KiB\n",
                    cart.getMapper(),
                    cart.getROM().size() / 1024,
                    cart.getVROM().size() / 1024);
    }

    std::uint64_t firstHash   = 0;
    std::uint64_t lastHash    = 0;
    std::size_t   totalAudio  = 0;
    int           audioRate   = 0;
    bool          allStripes  = true;

    for (int run = 0; run < repeats; ++run)
    {
        if (run > 0)
            emulator.reset();

        for (long f = 0; f < frames; ++f)
        {
            input.frame = static_cast<int>(f);
            emulator.runFrame();
        }

        const std::uint32_t* frame = emulator.frameBuffer();
        lastHash = fnv1a(frame, static_cast<std::size_t>(allnes::NESVideoWidth) * allnes::NESVideoHeight);
        if (run == 0)
            firstHash = lastHash;

        totalAudio = audio.totalSamples;
        audioRate  = audio.lastRate;

        if (check && !checkStripes(frame, allnes::NESVideoWidth, allnes::NESVideoHeight))
            allStripes = false;
    }

    if (printHash)
    {
        std::printf("hash: %016llx\n", static_cast<unsigned long long>(lastHash));
        std::printf("audio-hash: %016llx\n", static_cast<unsigned long long>(audio.hash));
    }

    if (printPC)
        std::printf("pc: %04x\n", emulator.cpuPC());

    std::printf("frames: %ld, audio samples: %zu @ %d Hz\n", frames * repeats, totalAudio, audioRate);

    bool failed = false;

    if (repeats > 1 && firstHash != lastHash)
    {
        std::fprintf(stderr, "FAIL: non-deterministic emulation (%016llx != %016llx)\n",
                     static_cast<unsigned long long>(firstHash),
                     static_cast<unsigned long long>(lastHash));
        failed = true;
    }

    if (!expectHash.empty())
    {
        char actual[17];
        std::snprintf(actual, sizeof(actual), "%016llx", static_cast<unsigned long long>(lastHash));
        if (expectHash != actual)
        {
            std::fprintf(stderr, "FAIL: hash %s != expected %s\n", actual, expectHash.c_str());
            failed = true;
        }
    }

    if (check && !allStripes)
    {
        std::fprintf(stderr, "FAIL: test-ROM stripe pattern check failed\n");
        failed = true;
    }

    if (!dumpPath.empty())
    {
        if (dumpPpm(dumpPath.c_str(), emulator.frameBuffer(), allnes::NESVideoWidth, allnes::NESVideoHeight))
            std::printf("wrote %s\n", dumpPath.c_str());
        else
        {
            std::fprintf(stderr, "FAIL: could not write %s\n", dumpPath.c_str());
            failed = true;
        }
    }

    std::printf("%s\n", failed ? "FAILED" : "OK");
    return failed ? 1 : 0;
}
