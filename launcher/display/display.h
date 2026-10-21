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
    int wheelX = 0;        // SDL sign: up is positive
    int wheelY = 0;

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

    virtual std::uint32_t ticks() const = 0;      // milliseconds
    virtual void          delay(std::uint32_t ms) = 0;

    // Backing canvas = design size * renderScale. The UI creates its display
    // at this size and scales layout metrics and fonts by renderScale.
    virtual int canvasWidth() const  = 0;
    virtual int canvasHeight() const = 0;
    virtual int renderScale() const  = 0;
};

std::unique_ptr<Display> createDisplay(Backend backend, std::string& error);
const char*              backendName(Backend backend);
} // namespace launcher::display
