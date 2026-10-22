#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The display layer sits below the launcher UI. It owns the window/backing
// surface, frame submission, event translation and timing, so the UI never
// includes SDL (or OpenGL/FFmpeg, once those backends exist).
//
// Pixel format is always ARGB8888 packed in a uint32_t as 0xAARRGGBB.
namespace launcher::display
{
struct Config
{
    std::string title = "AllNES 启动器";
    int  logicalWidth  = 1920; // design canvas (layout units)
    int  logicalHeight = 1080;
    int  windowWidth   = 1600;
    int  windowHeight  = 900;
    int  minWidth      = 960;
    int  minHeight     = 540;
    bool resizable     = true;
    bool highDpi       = true;
    bool vsync         = true;
};

enum class Backend
{
    Auto,
    Sdl,
    Headless,
};

enum class EventType
{
    None,
    Quit,
    KeyDown,
    TextInput,
    MouseMove,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
    FocusLost,
    Expose,
    Resize,
};

// Portable key set: only keys the launcher actually reacts to. Text input
// (search box) arrives separately as EventType::TextInput.
enum class Key
{
    Unknown,
    Escape,
    Enter,
    Tab,
    Up,
    Down,
    Left,
    Right,
    PageUp,
    PageDown,
    Home,
    End,
    Backspace,
    F5,
    F11,
    Q,
    A,
    D,
    S,
    W,
    F,
    Slash,
    LeftBracket,
    RightBracket,
};

struct Event
{
    EventType type = EventType::None;
    Key       key  = Key::Unknown;
    bool      repeat = false;
    bool      ctrl   = false;
    bool      shift  = false;
    bool      alt    = false;
    std::string text;      // TextInput: UTF-8 characters

    int x = 0;             // mouse position in canvas pixels
    int y = 0;
    // Precise wheel deltas (SDL sign: up is positive). Trackpads report
    // fractional amounts; rounding them per event made the UI scroll one row
    // for every event of a single flick.
    float wheelX = 0.0f;
    float wheelY = 0.0f;

    int windowWidth  = 0;  // Resize
    int windowHeight = 0;
};

class Display
{
public:
    virtual ~Display() = default;

    virtual bool init(const Config& config, std::string& error) = 0;
    virtual void shutdown() = 0;
    virtual bool ready() const = 0;

    // Stage a dirty rectangle. present() is what makes the frame visible.
    virtual void updateRect(int x, int y, int w, int h,
                            const void* pixels, int stride) = 0;
    virtual void present() = 0;

    // Read the complete current frame without presenting it (screenshots).
    virtual bool readPixels(std::vector<std::uint32_t>& out,
                            int& width, int& height) = 0;

    // Returns false when the event queue is drained.
    virtual bool pollEvent(Event& out) = 0;
    virtual void setTextInput(bool enabled) = 0;

    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void setFullscreen(bool enabled) = 0;
    virtual bool fullscreen() const = 0;

    // Native directory chooser owned by the window layer. `initialDir` seeds
    // the dialog (may be empty). Returns true and fills `path` when the user
    // picks a folder; returns false on cancel (`error` stays empty) or when no
    // picker is available (`error` is set). `lastDir`, when non-null, receives
    // the directory the dialog ended in so callers can remember it next time.
    virtual bool pickFolder(const std::string& initialDir,
                            std::string& path, std::string& error,
                            std::string* lastDir = nullptr) = 0;

    virtual std::uint32_t ticks() const = 0;      // milliseconds
    virtual void          delay(std::uint32_t ms) = 0;

    // Backing canvas = design size * renderScale. The UI creates its display
    // at this size and scales layout metrics and fonts by renderScale. The
    // scale is fractional (e.g. 0.833 on a 1600x900 window) so the canvas can
    // match the window's drawable pixels 1:1 and avoid blurry resampling.
    virtual int   canvasWidth() const  = 0;
    virtual int   canvasHeight() const = 0;
    virtual float renderScale() const  = 0;

    // Raw gamepad state for the settings "input" panel. Indices are portable
    // (below); the SDL backend maps them to SDL_GameControllerButton/Axis so
    // the UI stays SDL-free. Every accessor is safe for an empty slot.
    virtual int         padCount() const = 0;
    virtual bool        padButton(int slot, int buttonIndex) const = 0;
    virtual int         padAxis(int slot, int axisIndex) const = 0;
    virtual std::string padName(int slot) const = 0;
    virtual bool        padRumble(int slot) = 0;

    // When false, gamepad input no longer drives launcher navigation; it is
    // still readable through padButton()/padAxis(). The settings "gamepad test"
    // panel turns this off so testing a pad does not also move the selection,
    // leave the screen or launch games.
    virtual void setPadNavigationEnabled(bool enabled) = 0;
};

// Portable gamepad button indices (padButton). Matches SDL's abstract
// controller layout: A/B/X/Y, shoulders, back/start, sticks, guide, D-pad.
enum PadButton
{
    PadA = 0, PadB, PadX, PadY,
    PadLeftShoulder, PadRightShoulder,
    PadBack, PadStart,
    PadLeftStick, PadRightStick,
    PadGuide,
    PadDpadUp, PadDpadDown, PadDpadLeft, PadDpadRight,
    PadButtonCount,
};

// Portable gamepad axis indices (padAxis).
enum PadAxis
{
    PadLeftX = 0, PadLeftY, PadRightX, PadRightY,
    PadTriggerLeft, PadTriggerRight,
    PadAxisCount,
};

std::unique_ptr<Display> createDisplay(Backend backend, std::string& error);
const char*              backendName(Backend backend);
} // namespace launcher::display
