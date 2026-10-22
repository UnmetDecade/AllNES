#include "launcher/display/display_sdl.h"

#include "launcher/display/folder_picker.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace launcher::display
{
namespace
{
bool inputDebugEnabled()
{
    static const bool enabled = [] {
        const char* value = std::getenv("ALLNES_LAUNCHER_INPUT_DEBUG");
        return value && *value && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

Key keyFromScancode(SDL_Scancode scancode)
{
    switch (scancode)
    {
    case SDL_SCANCODE_ESCAPE:       return Key::Escape;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:     return Key::Enter;
    case SDL_SCANCODE_TAB:          return Key::Tab;
    case SDL_SCANCODE_UP:           return Key::Up;
    case SDL_SCANCODE_DOWN:         return Key::Down;
    case SDL_SCANCODE_LEFT:         return Key::Left;
    case SDL_SCANCODE_RIGHT:        return Key::Right;
    case SDL_SCANCODE_PAGEUP:       return Key::PageUp;
    case SDL_SCANCODE_PAGEDOWN:     return Key::PageDown;
    case SDL_SCANCODE_HOME:         return Key::Home;
    case SDL_SCANCODE_END:          return Key::End;
    case SDL_SCANCODE_BACKSPACE:    return Key::Backspace;
    case SDL_SCANCODE_F5:           return Key::F5;
    case SDL_SCANCODE_F11:          return Key::F11;
    case SDL_SCANCODE_Q:            return Key::Q;
    case SDL_SCANCODE_A:            return Key::A;
    case SDL_SCANCODE_D:            return Key::D;
    case SDL_SCANCODE_S:            return Key::S;
    case SDL_SCANCODE_W:            return Key::W;
    case SDL_SCANCODE_F:            return Key::F;
    case SDL_SCANCODE_SLASH:        return Key::Slash;
    case SDL_SCANCODE_LEFTBRACKET:  return Key::LeftBracket;
    case SDL_SCANCODE_RIGHTBRACKET: return Key::RightBracket;
    default:                        return Key::Unknown;
    }
}

// Controller buttons are mapped onto the launcher's portable key set, so the
// UI needs no controller-specific handling.
Key keyFromPadButton(SDL_GameControllerButton button)
{
    switch (button)
    {
    case SDL_CONTROLLER_BUTTON_A:             return Key::Enter;
    case SDL_CONTROLLER_BUTTON_B:             return Key::Escape;
    case SDL_CONTROLLER_BUTTON_X:             return Key::F5;
    case SDL_CONTROLLER_BUTTON_Y:             return Key::Tab;
    case SDL_CONTROLLER_BUTTON_BACK:          return Key::F11;
    case SDL_CONTROLLER_BUTTON_START:         return Key::Enter;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return Key::LeftBracket;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return Key::RightBracket;
    case SDL_CONTROLLER_BUTTON_GUIDE:         return Key::Escape;
    default:                                  return Key::Unknown;
    }
}

Event padKeyDown(Key key, bool repeat)
{
    Event event;
    event.type   = EventType::KeyDown;
    event.key    = key;
    event.repeat = repeat;
    return event;
}

// Left stick dead zone and held-direction auto-repeat timing (ms).
constexpr Sint16       kPadDeadZone    = 8000;
constexpr std::uint32_t kPadRepeatDelay = 420;
constexpr std::uint32_t kPadRepeatRate  = 140;

// Portable PadButton/PadAxis indices -> SDL abstract controller symbols.
const SDL_GameControllerButton kSdlButtons[PadButtonCount] = {
    SDL_CONTROLLER_BUTTON_A,            // PadA
    SDL_CONTROLLER_BUTTON_B,            // PadB
    SDL_CONTROLLER_BUTTON_X,            // PadX
    SDL_CONTROLLER_BUTTON_Y,            // PadY
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER, // PadLeftShoulder
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,// PadRightShoulder
    SDL_CONTROLLER_BUTTON_BACK,         // PadBack
    SDL_CONTROLLER_BUTTON_START,        // PadStart
    SDL_CONTROLLER_BUTTON_LEFTSTICK,    // PadLeftStick
    SDL_CONTROLLER_BUTTON_RIGHTSTICK,   // PadRightStick
    SDL_CONTROLLER_BUTTON_GUIDE,        // PadGuide
    SDL_CONTROLLER_BUTTON_DPAD_UP,      // PadDpadUp
    SDL_CONTROLLER_BUTTON_DPAD_DOWN,    // PadDpadDown
    SDL_CONTROLLER_BUTTON_DPAD_LEFT,    // PadDpadLeft
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT,   // PadDpadRight
};

const SDL_GameControllerAxis kSdlAxes[PadAxisCount] = {
    SDL_CONTROLLER_AXIS_LEFTX,        // PadLeftX
    SDL_CONTROLLER_AXIS_LEFTY,        // PadLeftY
    SDL_CONTROLLER_AXIS_RIGHTX,       // PadRightX
    SDL_CONTROLLER_AXIS_RIGHTY,       // PadRightY
    SDL_CONTROLLER_AXIS_TRIGGERLEFT,  // PadTriggerLeft
    SDL_CONTROLLER_AXIS_TRIGGERRIGHT, // PadTriggerRight
};
} // namespace

SdlDisplay::~SdlDisplay()
{
    shutdown();
}

bool SdlDisplay::init(const Config& config, std::string& error)
{
    if (m_window && m_renderer && m_texture)
        return true;

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
    {
        error = std::string("SDL video init failed: ") + SDL_GetError();
        return false;
    }
    m_videoReady = true;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    // Keep touch and mouse event streams separate. Touch arrives as
    // SDL_FINGER* events; letting SDL also synthesize mouse events from them
    // would send every tap through the pointer twice and could release the
    // button at a stale coordinate.
#ifdef SDL_HINT_TOUCH_MOUSE_EVENTS
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif
#ifdef SDL_HINT_MOUSE_TOUCH_EVENTS
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#endif

    Uint32 windowFlags = SDL_WINDOW_SHOWN;
    if (config.resizable)
        windowFlags |= SDL_WINDOW_RESIZABLE;
    if (config.highDpi)
        windowFlags |= SDL_WINDOW_ALLOW_HIGHDPI;

    int windowW = config.windowWidth;
    int windowH = config.windowHeight;
    // Keep the default window at 16:9 and shrink it to fit the display's
    // usable area. If the OS clamps the window to a non-16:9 size, the 16:9
    // canvas is letterboxed and dark bars appear down the sides.
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && usable.w > 0 && usable.h > 0)
    {
        const int availW = usable.w - 48;
        const int availH = usable.h - 48;
        if (availW < windowW || availH < windowH)
        {
            const double s = std::min(static_cast<double>(availW) / windowW,
                                      static_cast<double>(availH) / windowH);
            windowW = std::max(config.minWidth,  static_cast<int>(windowW * s));
            windowH = std::max(config.minHeight, static_cast<int>(windowH * s));
        }
    }

    m_window = SDL_CreateWindow(config.title.c_str(),
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                windowW, windowH,
                                windowFlags);
    if (!m_window)
    {
        error = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
        shutdown();
        return false;
    }

    Uint32 rendererFlags = SDL_RENDERER_ACCELERATED;
    if (config.vsync)
        rendererFlags |= SDL_RENDERER_PRESENTVSYNC;

    m_renderer = SDL_CreateRenderer(m_window, -1, rendererFlags);
    if (!m_renderer)
        m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_SOFTWARE);
    if (!m_renderer)
    {
        error = std::string("SDL_CreateRenderer failed: ") + SDL_GetError();
        shutdown();
        return false;
    }

    // Size the canvas to the window's actual drawable area so SDL never has to
    // resample the UI: the texture maps 1:1 onto the screen and the renderer
    // letterboxes any leftover area. `renderScale` becomes fractional (e.g.
    // 0.833 on a 1600x900 window) and drives the UI's layout/font scaling.
    int drawableW = config.windowWidth;
    int drawableH = config.windowHeight;
    if (SDL_GetRendererOutputSize(m_renderer, &drawableW, &drawableH) != 0 ||
        drawableW <= 0 || drawableH <= 0)
    {
        drawableW = config.windowWidth;
        drawableH = config.windowHeight;
    }

    const int designW = config.logicalWidth  > 0 ? config.logicalWidth  : 1920;
    const int designH = config.logicalHeight > 0 ? config.logicalHeight : 1080;

    // A uniform scale keeps the design's 16:9 aspect ratio; the renderer's
    // logical-size letterboxing centers the result and fills the rest.
    double fitScale = std::min(static_cast<double>(drawableW) / designW,
                               static_cast<double>(drawableH) / designH);
    if (fitScale <= 0.0)
        fitScale = 1.0;
    // Render at 2x on windows smaller than the design canvas so text is
    // rasterized large and SDL downscales 2:1 (supersampled, sharp). At or
    // above design size the canvas maps 1:1 onto the drawable.
    const double scale = fitScale >= 1.0 ? fitScale : 2.0 * fitScale;
    m_renderScale  = static_cast<float>(scale);
    m_canvasWidth  = std::max(1, static_cast<int>(std::lround(designW * scale)));
    m_canvasHeight = std::max(1, static_cast<int>(std::lround(designH * scale)));

    m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING,
                                  m_canvasWidth, m_canvasHeight);
    if (!m_texture)
    {
        error = std::string("SDL_CreateTexture failed: ") + SDL_GetError();
        shutdown();
        return false;
    }
    SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_NONE);

    // This makes SDL install its renderer event watch, which rewrites mouse
    // and touch event coordinates into canvas pixels before we see them.
    if (SDL_RenderSetLogicalSize(m_renderer, m_canvasWidth, m_canvasHeight) != 0)
    {
        error = std::string("SDL_RenderSetLogicalSize failed: ") + SDL_GetError();
        shutdown();
        return false;
    }

    SDL_SetWindowMinimumSize(m_window, config.minWidth, config.minHeight);
    m_start = std::chrono::steady_clock::now();

    // Controllers are optional: the launcher stays fully usable with the
    // keyboard/mouse when none is attached. init() logs its own failure.
    m_pads.init();
    return true;
}

void SdlDisplay::shutdown()
{
    if (m_texture)
    {
        SDL_DestroyTexture(m_texture);
        m_texture = nullptr;
    }
    if (m_renderer)
    {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    if (m_window)
    {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    if (m_videoReady)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        m_videoReady = false;
    }
    m_pads.shutdown();
    resetPadState();
    m_canvasWidth  = 0;
    m_canvasHeight = 0;
    m_renderScale  = 1.0f;
    m_fullscreen   = false;
}

bool SdlDisplay::ready() const
{
    return m_window && m_renderer && m_texture;
}

void SdlDisplay::windowToCanvas(float wx, float wy, int& cx, int& cy) const
{
    if (!m_renderer)
    {
        cx = static_cast<int>(std::lround(wx));
        cy = static_cast<int>(std::lround(wy));
        return;
    }

    float logicalX = wx;
    float logicalY = wy;
    SDL_RenderWindowToLogical(m_renderer, wx, wy, &logicalX, &logicalY);
    cx = static_cast<int>(std::lround(logicalX));
    cy = static_cast<int>(std::lround(logicalY));
}

void SdlDisplay::debugInput(const char* kind, float rawX, float rawY,
                            int cx, int cy) const
{
    if (!inputDebugEnabled())
        return;

    int windowW = 0;
    int windowH = 0;
    SDL_GetWindowSize(m_window, &windowW, &windowH);

    int outputW = 0;
    int outputH = 0;
    SDL_GetRendererOutputSize(m_renderer, &outputW, &outputH);

    std::fprintf(stderr,
                 "AllNES input %s: event=(%.3f,%.3f) windowSize=%dx%d "
                 "drawable=%dx%d canvas=%dx%d -> canvas=(%d,%d)\n",
                 kind, rawX, rawY, windowW, windowH, outputW, outputH,
                 m_canvasWidth, m_canvasHeight, cx, cy);
}

void SdlDisplay::updateRect(int x, int y, int w, int h,
                            const void* pixels, int stride)
{
    if (!m_texture || !pixels || w <= 0 || h <= 0)
        return;

    SDL_Rect rect { x, y, w, h };
    SDL_UpdateTexture(m_texture, &rect, pixels, stride);
}

void SdlDisplay::compose()
{
    if (!m_renderer || !m_texture)
        return;

    SDL_SetRenderDrawColor(m_renderer, 13, 14, 18, 255);
    SDL_RenderClear(m_renderer);
    SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
}

void SdlDisplay::present()
{
    if (!m_renderer)
        return;

    compose();
    SDL_RenderPresent(m_renderer);
}

bool SdlDisplay::readPixels(std::vector<std::uint32_t>& out,
                            int& width, int& height)
{
    if (!m_renderer)
        return false;

    int outputWidth  = 0;
    int outputHeight = 0;
    if (SDL_GetRendererOutputSize(m_renderer, &outputWidth, &outputHeight) != 0 ||
        outputWidth <= 0 || outputHeight <= 0)
    {
        return false;
    }

    // Reading after SDL_RenderPresent() is undefined, so compose the current
    // texture into the back buffer and read that.
    compose();

    out.resize(static_cast<std::size_t>(outputWidth) *
               static_cast<std::size_t>(outputHeight));
    if (SDL_RenderReadPixels(m_renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                             out.data(), outputWidth * 4) != 0)
    {
        out.clear();
        return false;
    }

    width  = outputWidth;
    height = outputHeight;
    return true;
}

bool SdlDisplay::pollEvent(Event& out)
{
    // Emit queued controller navigation events first, then any freshly
    // detected edges/repeats, before draining the SDL event queue.
    if (!m_padQueue.empty())
    {
        out = m_padQueue.front();
        m_padQueue.pop_front();
        return true;
    }
    if (m_padNavEnabled && m_pads.count() > 0)
        controllerTick();
    if (!m_padQueue.empty())
    {
        out = m_padQueue.front();
        m_padQueue.pop_front();
        return true;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_JOYDEVICEADDED || event.type == SDL_CONTROLLERDEVICEADDED ||
            event.type == SDL_JOYDEVICEREMOVED || event.type == SDL_CONTROLLERDEVICEREMOVED)
        {
            m_pads.handleEvent(event);
            continue; // device events are not launcher events
        }
        if (translate(event, out))
            return true;
    }
    return false;
}

void SdlDisplay::controllerTick()
{
    constexpr int kSlot = 0; // the first pad drives launcher navigation
    const std::uint32_t now = ticks();

    // One-shot buttons fire on the press edge only, so holding A does not spam
    // Enter. Their targets already ignore repeats, but the edge keeps it clean.
    static const SDL_GameControllerButton kOneShot[] = {
        SDL_CONTROLLER_BUTTON_A,
        SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_X,
        SDL_CONTROLLER_BUTTON_Y,
        SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_START,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        SDL_CONTROLLER_BUTTON_GUIDE,
    };
    for (SDL_GameControllerButton button : kOneShot)
    {
        const bool down = m_pads.button(kSlot, button);
        if (down && !m_padBtnPrev[button])
        {
            const Key key = keyFromPadButton(button);
            if (key != Key::Unknown)
                m_padQueue.push_back(padKeyDown(key, false));
        }
        m_padBtnPrev[button] = down ? 1 : 0;
    }

    // Directions: D-pad or left stick. A press edge fires once; holding
    // auto-repeats so the selection keeps scrolling.
    const bool dir[4] = {
        m_pads.button(kSlot, SDL_CONTROLLER_BUTTON_DPAD_UP)    || stickDir(kSlot, 0),
        m_pads.button(kSlot, SDL_CONTROLLER_BUTTON_DPAD_DOWN)  || stickDir(kSlot, 1),
        m_pads.button(kSlot, SDL_CONTROLLER_BUTTON_DPAD_LEFT)  || stickDir(kSlot, 2),
        m_pads.button(kSlot, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || stickDir(kSlot, 3),
    };
    static const Key kDirKeys[4] = { Key::Up, Key::Down, Key::Left, Key::Right };
    for (int d = 0; d < 4; ++d)
    {
        if (dir[d] && !m_dirPrev[d])
        {
            m_padQueue.push_back(padKeyDown(kDirKeys[d], false));
            m_dirDownAt[d]     = now;
            m_dirLastRepeat[d] = now;
        }
        else if (dir[d] && m_dirPrev[d] &&
                 now - m_dirDownAt[d] >= kPadRepeatDelay &&
                 now - m_dirLastRepeat[d] >= kPadRepeatRate)
        {
            m_padQueue.push_back(padKeyDown(kDirKeys[d], true));
            m_dirLastRepeat[d] = now;
        }
        m_dirPrev[d] = dir[d];
    }
}

bool SdlDisplay::stickDir(int slot, int dirIndex) const
{
    const Sint16 x = m_pads.axis(slot, SDL_CONTROLLER_AXIS_LEFTX);
    const Sint16 y = m_pads.axis(slot, SDL_CONTROLLER_AXIS_LEFTY);
    switch (dirIndex)
    {
    case 0: return y < -kPadDeadZone;  // up is negative Y
    case 1: return y >  kPadDeadZone;
    case 2: return x < -kPadDeadZone;
    default: return x >  kPadDeadZone; // right
    }
}

void SdlDisplay::resetPadState()
{
    m_padQueue.clear();
    std::memset(m_padBtnPrev, 0, sizeof(m_padBtnPrev));
    std::memset(m_dirPrev, 0, sizeof(m_dirPrev));
    std::memset(m_dirDownAt, 0, sizeof(m_dirDownAt));
    std::memset(m_dirLastRepeat, 0, sizeof(m_dirLastRepeat));
}

void SdlDisplay::setPadNavigationEnabled(bool enabled)
{
    m_padNavEnabled = enabled;
    if (!enabled)
        resetPadState(); // drop pending pad events and held-edge state
}

bool SdlDisplay::translate(const SDL_Event& event, Event& out) const
{
    switch (event.type)
    {
    case SDL_QUIT:
        out          = Event();
        out.type     = EventType::Quit;
        out.windowWidth  = m_canvasWidth;
        out.windowHeight = m_canvasHeight;
        return true;

    case SDL_KEYDOWN:
        out        = Event();
        out.type   = EventType::KeyDown;
        out.key    = keyFromScancode(event.key.keysym.scancode);
        out.repeat = event.key.repeat != 0;
        out.ctrl   = (event.key.keysym.mod & KMOD_CTRL) != 0;
        out.shift  = (event.key.keysym.mod & KMOD_SHIFT) != 0;
        out.alt    = (event.key.keysym.mod & KMOD_ALT) != 0;
        return true;

    case SDL_TEXTINPUT:
        out      = Event();
        out.type = EventType::TextInput;
        out.text = event.text.text;
        return true;

    case SDL_MOUSEMOTION:
        // SDL_RenderSetLogicalSize installs a renderer event watch that has
        // already rewritten window coordinates into canvas coordinates. Do
        // NOT convert again with SDL_RenderWindowToLogical: that was the
        // source of the offset, because it scaled an already-scaled value.
        out      = Event();
        out.type = EventType::MouseMove;
        out.x    = event.motion.x;
        out.y    = event.motion.y;
        return true;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        if (event.button.button != SDL_BUTTON_LEFT)
            return false;
        out      = Event();
        out.type = event.type == SDL_MOUSEBUTTONDOWN ? EventType::MouseButtonDown
                                                     : EventType::MouseButtonUp;
        out.x    = event.button.x;
        out.y    = event.button.y;
        debugInput(event.type == SDL_MOUSEBUTTONDOWN ? "mouse-down" : "mouse-up",
                   static_cast<float>(event.button.x),
                   static_cast<float>(event.button.y), out.x, out.y);
        return true;

    case SDL_FINGERDOWN:
    case SDL_FINGERMOTION:
    case SDL_FINGERUP:
    {
        // Reduce multi-touch to one finger. A second finger must not move the
        // pointer or release the first one.
        if (event.type == SDL_FINGERDOWN)
        {
            if (m_activeFinger >= 0)
                return false;
            m_activeFinger = event.tfinger.fingerId;
            m_hasFingerPos = false;
        }
        else if (event.tfinger.fingerId != m_activeFinger)
        {
            return false;
        }

        int windowW = 0;
        int windowH = 0;
        SDL_GetWindowSize(m_window, &windowW, &windowH);
        if (windowW <= 0 || windowH <= 0)
            return false;

        int cx = 0;
        int cy = 0;
        if (event.type != SDL_FINGERUP)
        {
            // SDL2 and sdl2-compat disagree about the coordinates they put
            // in SDL_FINGER* events: real SDL2's renderer event watch rewrites
            // them to 0..1 across the letterboxed viewport, while
            // sdl2-compat leaves the driver's window-relative 0..1 untouched.
            // The touch device state is window-relative in both
            // implementations and is not touched by the event watch, so read
            // it there and go through the same window -> canvas mapping as
            // the mouse. This keeps touch correct with letterboxing and
            // HiDPI on both.
            float nx = 0.0f;
            float ny = 0.0f;
            bool haveRaw = false;
            const int fingerCount =
                SDL_GetNumTouchFingers(event.tfinger.touchId);
            for (int i = 0; i < fingerCount && !haveRaw; ++i)
            {
                const SDL_Finger* finger =
                    SDL_GetTouchFinger(event.tfinger.touchId, i);
                if (finger && finger->id == event.tfinger.fingerId)
                {
                    nx = finger->x;
                    ny = finger->y;
                    haveRaw = true;
                }
            }

            if (haveRaw)
            {
                windowToCanvas(nx * static_cast<float>(windowW),
                               ny * static_cast<float>(windowH), cx, cy);
            }
            else
            {
                // Synthetic events have no touch state. Both variants give
                // 0..1 here; treating it as window-relative is exact on
                // sdl2-compat and exact on SDL2 whenever the canvas fills
                // the window (the common case).
                windowToCanvas(event.tfinger.x * static_cast<float>(windowW),
                               event.tfinger.y * static_cast<float>(windowH),
                               cx, cy);
            }

            m_fingerCanvasX = cx;
            m_fingerCanvasY = cy;
            m_hasFingerPos  = true;
        }
        else
        {
            // The finger is removed from the touch state before SDL lets us
            // poll the up event, so release at the last known position.
            if (m_hasFingerPos)
            {
                cx = m_fingerCanvasX;
                cy = m_fingerCanvasY;
            }
            else
            {
                windowToCanvas(event.tfinger.x * static_cast<float>(windowW),
                               event.tfinger.y * static_cast<float>(windowH),
                               cx, cy);
            }
            m_activeFinger = -1;
            m_hasFingerPos = false;
        }

        // A finger in the letterbox area is clamped onto the canvas edge.
        out      = Event();
        out.type = event.type == SDL_FINGERDOWN
                       ? EventType::MouseButtonDown
                       : (event.type == SDL_FINGERUP ? EventType::MouseButtonUp
                                                     : EventType::MouseMove);
        out.x    = std::clamp(cx, 0, m_canvasWidth  > 0 ? m_canvasWidth  - 1 : 0);
        out.y    = std::clamp(cy, 0, m_canvasHeight > 0 ? m_canvasHeight - 1 : 0);
        if (event.type != SDL_FINGERMOTION)
            debugInput(event.type == SDL_FINGERDOWN ? "touch-down" : "touch-up",
                       event.tfinger.x, event.tfinger.y, out.x, out.y);
        return true;
    }

    case SDL_MOUSEWHEEL:
        out      = Event();
        out.type = EventType::MouseWheel;
        // preciseX/preciseY keep the fractional trackpad deltas; x/y are
        // already rounded per event and would turn a single flick into
        // dozens of full-speed steps.
#if SDL_VERSION_ATLEAST(2, 0, 18)
        out.wheelX = event.wheel.preciseX != 0.0f ? event.wheel.preciseX
                                                  : static_cast<float>(event.wheel.x);
        out.wheelY = event.wheel.preciseY != 0.0f ? event.wheel.preciseY
                                                  : static_cast<float>(event.wheel.y);
        // Natural scrolling (the macOS default) flips the reported deltas;
        // normalize back to SDL's canonical "up is positive" convention so the
        // launcher scrolls the same direction on every platform.
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
        {
            out.wheelX = -out.wheelX;
            out.wheelY = -out.wheelY;
        }
#else
        out.wheelX = static_cast<float>(event.wheel.x);
        out.wheelY = static_cast<float>(event.wheel.y);
#endif
        return true;

    case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_LEAVE ||
            event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
        {
            // The finger-up may be delivered to another window; forget the
            // active finger so the next touch starts a fresh press.
            m_activeFinger = -1;
            m_hasFingerPos = false;
            out      = Event();
            out.type = EventType::FocusLost;
            return true;
        }
        if (event.window.event == SDL_WINDOWEVENT_EXPOSED ||
            event.window.event == SDL_WINDOWEVENT_RESTORED ||
            event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        {
            out      = Event();
            out.type = event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED
                         ? EventType::Resize
                         : EventType::Expose;
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            {
                out.windowWidth  = event.window.data1;
                out.windowHeight = event.window.data2;
            }
            return true;
        }
        return false;

    default:
        return false;
    }
}

void SdlDisplay::setTextInput(bool enabled)
{
    if (enabled)
        SDL_StartTextInput();
    else
        SDL_StopTextInput();
}

void SdlDisplay::show()
{
    if (!m_window)
        return;
    // A button release that happens while the window is hidden is never seen;
    // forget held state so a still-pressed button needs a fresh press.
    resetPadState();
    SDL_ShowWindow(m_window);
    SDL_RaiseWindow(m_window);
}

void SdlDisplay::hide()
{
    // Do not keep the IME attached to a hidden window: the emulator opens its
    // own window next and would inherit the active text-input state.
    SDL_StopTextInput();
    if (m_window)
        SDL_HideWindow(m_window);
    resetPadState();
}

void SdlDisplay::setFullscreen(bool enabled)
{
    if (!m_window)
        return;
    m_fullscreen = enabled;
    SDL_SetWindowFullscreen(m_window, enabled ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

bool SdlDisplay::fullscreen() const
{
    return m_fullscreen;
}

bool SdlDisplay::pickFolder(const std::string& initialDir,
                            std::string& path, std::string& error,
                            std::string* lastDir)
{
    return pickFolderNative(initialDir, path, error, lastDir);
}

std::uint32_t SdlDisplay::ticks() const
{
    const auto elapsed = std::chrono::steady_clock::now() - m_start;
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

void SdlDisplay::delay(std::uint32_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int SdlDisplay::canvasWidth() const
{
    return m_canvasWidth;
}

int SdlDisplay::canvasHeight() const
{
    return m_canvasHeight;
}

float SdlDisplay::renderScale() const
{
    return m_renderScale;
}

int SdlDisplay::padCount() const
{
    return m_pads.count();
}

bool SdlDisplay::padButton(int slot, int buttonIndex) const
{
    if (buttonIndex < 0 || buttonIndex >= PadButtonCount)
        return false;
    return m_pads.button(slot, kSdlButtons[buttonIndex]);
}

int SdlDisplay::padAxis(int slot, int axisIndex) const
{
    if (axisIndex < 0 || axisIndex >= PadAxisCount)
        return 0;
    return m_pads.axis(slot, kSdlAxes[axisIndex]);
}

std::string SdlDisplay::padName(int slot) const
{
    return m_pads.name(slot);
}

bool SdlDisplay::padRumble(int slot)
{
    return m_pads.rumble(slot, 0x8000, 0x8000, 300);
}
} // namespace launcher::display
