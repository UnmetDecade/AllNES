#include "keybindings.h"
#include "sdl_audio.h"
#include "sdl_input.h"
#include "sdl_video.h"

#include "Emulator.h"
#include "Log.h"

#include "launcher/display/display.h"
#include "launcher/model/paths.h"
#include "launcher/model/settings.h"

#ifdef ALLNES_HAVE_LVGL
#include "launcher/ui/lvgl_launcher.h"
#endif

#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace
{
const char* kVersion = "0.3.0";

// Timing model: one host iteration is 3 PPU dots + 1 CPU instruction
// + 1 APU step, and a frame is ~29781 such iterations of 559 ns each.
const long long kIterationsPerFrame = 29781;
const long long kCpuClockPeriodNs   = 559;

struct Options
{
    std::string romPath;
    std::string confPath;
    std::string libraryPath;
    launcher::display::Backend displayBackend = launcher::display::Backend::Auto;
    float       scale       = 3.0f;
    int         width       = 0;
    int         height      = 0;
    bool        fullscreen  = false;
    bool        muted       = false;
    bool        integerScale = false; // whole-pixel scaling instead of filling
    long        frames      = 0; // 0 = run until the window is closed
    long        menuFrames  = 0; // 0 = run the launcher until the user quits
};

void printUsage(const char* argv0)
{
    std::printf(
      "AllNES %s - a portable NES emulator\n\n"
      "Usage: %s [options] [rom.nes]\n\n"
      "Without a ROM the game launcher opens and lets you pick a game.\n\n"
      "Options:\n"
      "  -s, --scale N       Window scale (default 3)\n"
      "  -w, --width N       Window width in pixels (overrides --scale)\n"
      "  -H, --height N      Window height in pixels\n"
      "  -C, --conf FILE     Keybindings file (default: keybindings.conf)\n"
      "  -L, --library DIR   Game library folder for the launcher\n"
      "      --display NAME  Display backend: auto, sdl or headless (default auto)\n"
      "      --fullscreen    Start in fullscreen\n"
      "      --integer-scale Use whole-pixel scaling (may add black bars)\n"
      "      --mute-audio    Do not open an audio device\n"
      "      --frames N      Emulate N frames then exit (smoke tests)\n"
      "      --menu-frames N Draw N launcher frames then exit (smoke tests)\n"
      "  -h, --help          Show this help\n"
      "      --version       Show the version\n\n"
      "In a game: Esc or closing the window asks whether to return to the\n"
      "launcher or quit; F11 fullscreen, Ctrl+R reset, M mute.\n",
      kVersion,
      argv0);
}

bool parseOptions(int argc, char* argv[], Options& opts)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        auto needValue = [&](const char* name) -> const char* {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "%s: missing value for %s\n", argv[0], name);
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help")
        {
            printUsage(argv[0]);
            std::exit(0);
        }
        else if (arg == "--version")
        {
            std::printf("AllNES %s\n", kVersion);
            std::exit(0);
        }
        else if (arg == "-s" || arg == "--scale")
        {
            const char* v = needValue("--scale");
            if (!v) return false;
            opts.scale = std::max(1.0f, static_cast<float>(std::atof(v)));
        }
        else if (arg == "-w" || arg == "--width")
        {
            const char* v = needValue("--width");
            if (!v) return false;
            opts.width = std::atoi(v);
        }
        else if (arg == "-H" || arg == "--height")
        {
            const char* v = needValue("--height");
            if (!v) return false;
            opts.height = std::atoi(v);
        }
        else if (arg == "-C" || arg == "--conf")
        {
            const char* v = needValue("--conf");
            if (!v) return false;
            opts.confPath = v;
        }
        else if (arg == "-L" || arg == "--library")
        {
            const char* v = needValue("--library");
            if (!v) return false;
            opts.libraryPath = v;
        }
        else if (arg == "--display")
        {
            const char* v = needValue("--display");
            if (!v) return false;
            const std::string value = v;
            if (value == "auto")
                opts.displayBackend = launcher::display::Backend::Auto;
            else if (value == "sdl")
                opts.displayBackend = launcher::display::Backend::Sdl;
            else if (value == "headless")
                opts.displayBackend = launcher::display::Backend::Headless;
            else
            {
                std::fprintf(stderr, "%s: --display expects auto, sdl or headless\n", argv[0]);
                return false;
            }
        }
        else if (arg == "--fullscreen")
        {
            opts.fullscreen = true;
        }
        else if (arg == "--integer-scale")
        {
            opts.integerScale = true;
        }
        else if (arg == "--mute-audio")
        {
            opts.muted = true;
        }
        else if (arg == "--frames")
        {
            const char* v = needValue("--frames");
            if (!v) return false;
            opts.frames = std::atol(v);
        }
        else if (arg == "--menu-frames")
        {
            const char* v = needValue("--menu-frames");
            if (!v) return false;
            opts.menuFrames = std::atol(v);
        }
        else if (!arg.empty() && arg[0] == '-')
        {
            std::fprintf(stderr, "%s: unknown option '%s' (try --help)\n", argv[0], arg.c_str());
            return false;
        }
        else if (opts.romPath.empty())
        {
            opts.romPath = arg;
        }
        else
        {
            std::fprintf(stderr, "%s: unexpected argument '%s'\n", argv[0], arg.c_str());
            return false;
        }
    }

    return true;
}

// Look for keybindings.conf next to the executable when the CWD does not have
// one, so the binary can be launched from anywhere.
std::string defaultConfPath(const char* argv0)
{
    const std::string cwdConf = "keybindings.conf";
    if (std::FILE* f = std::fopen(cwdConf.c_str(), "r"))
    {
        std::fclose(f);
        return cwdConf;
    }

    if (char* base = SDL_GetBasePath())
    {
        const std::string candidate = std::string(base) + "keybindings.conf";
        SDL_free(base);
        if (std::FILE* f = std::fopen(candidate.c_str(), "r"))
        {
            std::fclose(f);
            return candidate;
        }
    }

    (void)argv0;
    return cwdConf;
}

void sleepUntil(std::chrono::steady_clock::time_point when)
{
    std::this_thread::sleep_until(when);
}

void windowSizeForScale(const Options& opts, int& width, int& height)
{
    width  = static_cast<int>(allnes::NESVideoWidth * opts.scale);
    height = static_cast<int>(allnes::NESVideoHeight * opts.scale);

    if (opts.width > 0)
    {
        width  = opts.width;
        height = opts.height > 0
                   ? opts.height
                   : opts.width * allnes::NESVideoHeight / allnes::NESVideoWidth;
    }
    else if (opts.height > 0)
    {
        height = opts.height;
        width  = opts.height * allnes::NESVideoWidth / allnes::NESVideoHeight;
    }
}

enum class GameResult
{
    Quit,       // close the program (or a direct command-line run ended)
    BackToMenu, // Esc in a game that was started from the launcher
    Error,      // the ROM or the window could not be created
};

// What to do after the player asked to leave a game.
enum class LeaveChoice
{
    Cancel,     // keep playing
    BackToMenu, // return to the launcher
    QuitDesktop // close the whole program
};

// Asks before leaving a game that was started from the launcher, so closing
// the window by mistake does not kill the launcher too. Returns BackToMenu
// when the platform cannot show a message box (Wayland and minimal drivers),
// which matches the old Esc behaviour.
LeaveChoice askLeaveGame(SDL_Window* window)
{
    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "返回启动器" },
        { 0,                                       1, "退出到桌面" },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 2, "取消" },
    };

    SDL_MessageBoxData data{};
    data.flags       = SDL_MESSAGEBOX_INFORMATION;
    data.window      = window;
    data.title       = "AllNES";
    data.message     = "要离开游戏吗？";
    data.numbuttons  = 3;
    data.buttons     = buttons;
    data.colorScheme = nullptr;

    int choice = -1;
    if (SDL_ShowMessageBox(&data, &choice) != 0 || choice < 0)
    {
        std::fprintf(stderr, "Could not show the leave dialog: %s\n",
                     SDL_GetError());
        return LeaveChoice::BackToMenu;
    }

    switch (choice)
    {
    case 0:  return LeaveChoice::BackToMenu;
    case 1:  return LeaveChoice::QuitDesktop;
    default: return LeaveChoice::Cancel;
    }
}

// Runs one game to completion. The sinks live for the duration of this
// function so the emulator is destroyed before them, and each game gets a
// fresh audio device, window and controller state.
GameResult runGame(const Options& opts,
                   const std::string& romPath,
                   int windowWidth,
                   int windowHeight,
                   bool fullscreen,
                   const host::Keybindings& keys,
                   bool returnToMenuOnEsc)
{
    host::SdlVideo video;
    host::SdlAudio audio;

    // The launcher's gamepad page owns stick/deadzone preferences; honour them
    // for games launched both from the UI and with --rom.
    const launcher::LauncherConfig launcherConfig =
        launcher::loadLauncherConfig(launcher::defaultLauncherConfigPath());
    host::PadOptions padOptions;
    padOptions.stickToDpad = launcherConfig.stickToDpad;
    padOptions.deadzone    = launcherConfig.deadzone;
    host::SdlInput input(keys, padOptions);

    if (!video.init(allnes::NESVideoWidth, allnes::NESVideoHeight,
                    windowWidth, windowHeight, fullscreen,
                    opts.integerScale))
        return GameResult::Error;

    const bool audioOk = audio.init(opts.muted);
    if (!audioOk)
        std::fprintf(stderr, "Continuing without audio.\n");

    input.init();

    allnes::Emulator emulator(video, audio, input);
    if (!audioOk)
        emulator.setMuted(true);

    if (!emulator.loadRom(romPath))
    {
        std::fprintf(stderr, "Could not load ROM '%s'\n", romPath.c_str());
        return GameResult::Error;
    }

    std::printf("AllNES %s | %s | %dx%d | mapper %d\n",
                kVersion,
                romPath.c_str(),
                allnes::NESVideoWidth,
                allnes::NESVideoHeight,
                emulator.cartridge().getMapper());

    bool running       = true;
    bool backToMenu    = false;
    bool quitRequested = false;
    bool muted         = opts.muted || !audioOk;
    long framesRun     = 0;

    const auto frameDuration = std::chrono::nanoseconds(kIterationsPerFrame * kCpuClockPeriodNs);
    auto       nextFrame     = std::chrono::steady_clock::now();

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            input.handleEvent(event);

            switch (event.type)
            {
            case SDL_QUIT:
                if (!returnToMenuOnEsc)
                {
                    running       = false;
                    quitRequested = true;
                    break;
                }
                switch (askLeaveGame(video.window()))
                {
                case LeaveChoice::Cancel:
                    break; // the window stays open, keep playing
                case LeaveChoice::BackToMenu:
                    running    = false;
                    backToMenu = true;
                    break;
                case LeaveChoice::QuitDesktop:
                    running       = false;
                    quitRequested = true;
                    break;
                }
                break;
            case SDL_KEYDOWN:
                if (event.key.repeat)
                    break;
                if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                {
                    if (!returnToMenuOnEsc)
                    {
                        running       = false;
                        quitRequested = true;
                    }
                    else
                    {
                        switch (askLeaveGame(video.window()))
                        {
                        case LeaveChoice::Cancel:
                            break;
                        case LeaveChoice::BackToMenu:
                            running    = false;
                            backToMenu = true;
                            break;
                        case LeaveChoice::QuitDesktop:
                            running       = false;
                            quitRequested = true;
                            break;
                        }
                    }
                }
                else if (event.key.keysym.scancode == SDL_SCANCODE_Q &&
                         (event.key.keysym.mod & KMOD_CTRL))
                {
                    running       = false;
                    quitRequested = true;
                }
                else if (event.key.keysym.scancode == SDL_SCANCODE_F11)
                {
                    video.setFullscreen(!video.isFullscreen());
                    input.poll(); // keep the newly-focused window in sync
                }
                else if (event.key.keysym.scancode == SDL_SCANCODE_R &&
                         (event.key.keysym.mod & KMOD_CTRL))
                {
                    emulator.reset();
                }
                else if (event.key.keysym.scancode == SDL_SCANCODE_M)
                {
                    audio.setMuted(!muted);
                    muted = audio.muted();
                    emulator.setMuted(muted);
                    std::printf("%s\n", muted ? "Audio muted" : "Audio on");
                }
                break;
            default:
                break;
            }
        }

        if (!running)
            break;

        input.poll();
        emulator.runFrame();
        ++framesRun;

        if (opts.frames > 0 && framesRun >= opts.frames)
            break;

        nextFrame += frameDuration;
        const auto now = std::chrono::steady_clock::now();
        if (nextFrame > now)
            sleepUntil(nextFrame);
        else if (now - nextFrame > frameDuration * 5)
            nextFrame = now; // the host stalled; resync instead of fast-forwarding
    }

    if (opts.frames > 0)
        std::printf("Ran %ld frames.\n", framesRun);

    if (quitRequested)
        return GameResult::Quit;
    if (backToMenu)
        return GameResult::BackToMenu;
    return GameResult::Quit; // frame limit reached
}
#ifdef ALLNES_HAVE_LVGL
int runLauncher(launcher::LvglLauncherApp& app,
                const Options& opts,
                const host::Keybindings& keys)
{
    if (!app.init(opts.libraryPath, opts.menuFrames, opts.displayBackend))
        return 1;

    for (;;)
    {
        launcher::LaunchRequest request;
        if (!app.run(request))
            break; // the user closed the launcher

        app.hideWindow();

        const GameResult result = runGame(opts, request.romPath, request.width, request.height,
                                          request.fullscreen, keys, true);

        app.showWindow();

        if (result == GameResult::Quit)
            break;
        if (result == GameResult::Error)
            app.setError("Could not load ROM '" + request.romPath + "'");
    }

    return 0;
}
#endif // ALLNES_HAVE_LVGL
} // namespace

int main(int argc, char* argv[])
{
    Options opts;
    if (!parseOptions(argc, argv, opts))
        return 2;

    allnes::Log::get().setLogStream(std::cerr);
    allnes::Log::get().setLevel(allnes::Error);

    if (SDL_Init(0) != 0)
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    host::Keybindings keys = host::defaultKeybindings();
    host::loadKeybindings(opts.confPath.empty() ? defaultConfPath(argv[0]) : opts.confPath, keys);

    // A ROM on the command line keeps the old one-shot behaviour: Esc quits.
    if (!opts.romPath.empty())
    {
        int windowWidth  = 0;
        int windowHeight = 0;
        windowSizeForScale(opts, windowWidth, windowHeight);

        const GameResult result = runGame(opts, opts.romPath, windowWidth, windowHeight,
                                          opts.fullscreen, keys, false);
        return result == GameResult::Error ? 1 : 0;
    }

    // No ROM: open the LVGL launcher and play whatever it returns.
#ifdef ALLNES_HAVE_LVGL
    launcher::LvglLauncherApp app;
    return runLauncher(app, opts, keys);
#else
    std::fprintf(stderr, "No launcher in this build (LVGL/FreeType unavailable). "
                         "Pass a ROM on the command line.\n");
    return 1;
#endif
}
