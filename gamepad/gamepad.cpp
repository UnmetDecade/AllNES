#include "gamepad/gamepad.h"
#include "gamepad/mapping.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace allnes::gamepad
{
namespace
{
bool debugEnabled()
{
    static const bool enabled = [] {
        const char* value = std::getenv("ALLNES_GAMEPAD_DEBUG");
        return value && *value && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

void debugf(const char* format, ...)
{
    if (!debugEnabled())
        return;

    std::fprintf(stderr, "gamepad: ");
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
}

std::string joinPath(const char* base, const char* leaf)
{
    if (!base || !*base)
        return std::string();
    return std::string(base) + leaf;
}

// Mapping database lookup order. The bundled copy lives next to the executable
// (build tree / install bin); packaged Linux installs keep it in share/allnes;
// users can drop an updated copy into their pref directory.
void loadMappingDatabase()
{
    const char* override = std::getenv("ALLNES_GAMECONTROLLERDB");
    if (override && *override)
    {
        const int added = SDL_GameControllerAddMappingsFromFile(override);
        if (added < 0)
            std::fprintf(stderr, "gamepad: could not load mapping DB '%s': %s\n",
                         override, SDL_GetError());
        else
            debugf("mapping DB: %s (+%d entries)\n", override, added);
        return;
    }

    std::string candidates[3];
    if (char* base = SDL_GetBasePath())
    {
        candidates[0] = joinPath(base, "gamecontrollerdb.txt");
        candidates[1] = joinPath(base, "../share/allnes/gamecontrollerdb.txt");
        SDL_free(base);
    }
    if (char* pref = SDL_GetPrefPath("AllNES", "AllNES"))
    {
        candidates[2] = joinPath(pref, "gamecontrollerdb.txt");
        SDL_free(pref);
    }

    for (const std::string& path : candidates)
    {
        if (path.empty())
            continue;

        const int added = SDL_GameControllerAddMappingsFromFile(path.c_str());
        if (added >= 0)
        {
            debugf("mapping DB: %s (+%d entries)\n", path.c_str(), added);
            return;
        }
    }

    debugf("no controller mapping DB found; using SDL built-ins only\n");
}

struct DeviceInfo
{
    bool         opened = false;
    std::string  name   = "unknown";
    std::string  guid;
    MappingCounts counts;
};

DeviceInfo queryDevice(int deviceIndex)
{
    DeviceInfo info;

    SDL_Joystick* joy = SDL_JoystickOpen(deviceIndex);
    if (!joy)
        return info;

    info.opened = true;
    if (const char* name = SDL_JoystickName(joy))
        info.name = name;

    char guid[64];
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joy), guid, sizeof(guid));
    info.guid = guid;

    info.counts.buttons = SDL_JoystickNumButtons(joy);
    info.counts.axes    = SDL_JoystickNumAxes(joy);
    info.counts.hats    = SDL_JoystickNumHats(joy);

    SDL_JoystickClose(joy);
    return info;
}
} // namespace

Gamepads::~Gamepads()
{
    shutdown();
}

bool Gamepads::init()
{
    // Set before the game controller subsystem starts so the hints apply to the
    // initial enumeration. The DualShock 3's USB link on macOS is fragile and
    // drops as soon as SDL stops reading its reports, so keep events flowing
    // when the window is unfocused and pin the PS3 HIDAPI driver on.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS3, "1");

    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
    {
        std::fprintf(stderr, "gamepad: SDL game controller init failed: %s\n",
                     SDL_GetError());
        return false;
    }

    // SDL frees the mapping list when the gamecontroller subsystem shuts down,
    // so this must run on every init rather than once per process.
    loadMappingDatabase();

    for (int i = 0; i < SDL_NumJoysticks(); ++i)
    {
        if (count() >= kMaxControllers)
            break;
        open(i);
    }
    return true;
}

void Gamepads::shutdown()
{
    for (int slot = 0; slot < kMaxControllers; ++slot)
        closeSlot(slot);

    // Deliberately skip SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER): on macOS
    // its IOKit teardown (DARWIN_JoystickQuit -> IOHIDManagerUnscheduleFromRunLoop)
    // crashes with EXC_BAD_ACCESS when a HID game controller is attached (seen
    // with a GameSir G7 Pro). shutdown() only runs as the process exits, so the
    // OS reclaims the subsystem; closing the controllers above is all we need.
}

int Gamepads::findEmptySlot() const
{
    for (int slot = 0; slot < kMaxControllers; ++slot)
        if (!m_controllers[slot])
            return slot;
    return -1;
}

bool Gamepads::prepareDevice(int deviceIndex)
{
    DeviceInfo info = queryDevice(deviceIndex);

    if (!SDL_IsGameController(deviceIndex))
    {
        if (!info.opened)
        {
            debugf("device %d: could not open joystick: %s\n", deviceIndex,
                   SDL_GetError());
            return false;
        }

        const std::string mapping = buildGenericMapping(info.guid, info.counts);
        const int added = SDL_GameControllerAddMapping(mapping.c_str());
        debugf("device %d '%s' (%s) is not in the mapping DB; generic mapping %s\n",
               deviceIndex, info.name.c_str(), info.guid.c_str(),
               added >= 0 ? "added" : "rejected");
    }

    if (!SDL_IsGameController(deviceIndex))
    {
        std::fprintf(stderr,
                     "gamepad: device %d '%s' has no usable game controller mapping\n",
                     deviceIndex, info.name.c_str());
        return false;
    }

    // Some community entries only describe part of the D-pad (notably macOS
    // Xbox 360 pads). Replace those fields with a complete set before the
    // controller is opened.
    const SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(deviceIndex);
    if (char* current = SDL_GameControllerMappingForGUID(guid))
    {
        const std::string mapping(current);
        const DpadCoverage coverage = parseDpadCoverage(mapping);
        if (!coverage.complete())
        {
            const std::string patched = completeDpadMapping(mapping, info.counts);
            if (!patched.empty())
            {
                const int added = SDL_GameControllerAddMapping(patched.c_str());
                debugf("device %d '%s': D-pad %s%s%s%s; patched mapping %s\n",
                       deviceIndex, info.name.c_str(),
                       coverage.up ? "U" : "-", coverage.down ? "D" : "-",
                       coverage.left ? "L" : "-", coverage.right ? "R" : "-",
                       added >= 0 ? "added" : "rejected");
            }
            else
            {
                std::fprintf(stderr,
                             "gamepad: '%s' has an incomplete D-pad and no hat/buttons "
                             "to complete it\n",
                             info.name.c_str());
            }
        }
        SDL_free(current);
    }
    else
    {
        debugf("device %d '%s': SDL has no mapping string for this GUID\n",
               deviceIndex, info.name.c_str());
    }

    debugf("device %d: '%s' guid=%s buttons=%d axes=%d hats=%d\n",
           deviceIndex, info.name.c_str(), info.guid.c_str(),
           info.counts.buttons, info.counts.axes, info.counts.hats);
    return true;
}

void Gamepads::open(int deviceIndex)
{
    const int slot = findEmptySlot();
    if (slot < 0)
        return;

    if (!prepareDevice(deviceIndex))
        return;

    SDL_GameController* controller = SDL_GameControllerOpen(deviceIndex);
    if (!controller)
        return;

    const SDL_JoystickID id =
        SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));

    // Never open the same physical device twice (an ADD event can arrive both
    // as SDL_JOYDEVICEADDED and SDL_CONTROLLERDEVICEADDED).
    for (int i = 0; i < kMaxControllers; ++i)
    {
        if (m_controllers[i] && m_instanceIds[i] == id)
        {
            SDL_GameControllerClose(controller);
            return;
        }
    }

    m_controllers[slot] = controller;
    m_instanceIds[slot] = id;
    std::fprintf(stderr, "gamepad: player %d = %s\n",
                 slot + 1, SDL_GameControllerName(controller));
    debugf("player %d mapping: %s\n", slot + 1, mapping(slot).c_str());
}

void Gamepads::closeSlot(int slot)
{
    if (m_controllers[slot])
    {
        debugf("player %d disconnected: %s\n", slot + 1,
               SDL_GameControllerName(m_controllers[slot]));
        SDL_GameControllerClose(m_controllers[slot]);
        m_controllers[slot] = nullptr;
        m_instanceIds[slot] = 0;
    }
}

int Gamepads::count() const
{
    int n = 0;
    for (int slot = 0; slot < kMaxControllers; ++slot)
        if (m_controllers[slot])
            ++n;
    return n;
}

void Gamepads::handleEvent(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_JOYDEVICEADDED:
    case SDL_CONTROLLERDEVICEADDED:
        if (count() < kMaxControllers)
            open(event.type == SDL_JOYDEVICEADDED ? event.jdevice.which
                                                  : event.cdevice.which);
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
    case SDL_JOYDEVICEREMOVED:
    {
        const SDL_JoystickID id = event.type == SDL_CONTROLLERDEVICEREMOVED
                                    ? event.cdevice.which
                                    : event.jdevice.which;
        for (int slot = 0; slot < kMaxControllers; ++slot)
        {
            if (m_controllers[slot] && m_instanceIds[slot] == id)
            {
                closeSlot(slot);
                break;
            }
        }
        break;
    }
    default:
        break;
    }
}

bool Gamepads::rawHat(int slot, Uint8 mask) const
{
    if (slot < 0 || slot >= kMaxControllers || !m_controllers[slot])
        return false;

    SDL_Joystick* joy = SDL_GameControllerGetJoystick(m_controllers[slot]);
    if (!joy)
        return false;

    const int hats = SDL_JoystickNumHats(joy);
    for (int h = 0; h < hats; ++h)
    {
        if (SDL_JoystickGetHat(joy, h) & mask)
            return true;
    }
    return false;
}

bool Gamepads::button(int slot, SDL_GameControllerButton b) const
{
    if (slot < 0 || slot >= kMaxControllers || !m_controllers[slot])
        return false;

    Uint8 mask = 0;
    switch (b)
    {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        mask = SDL_HAT_UP;
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        mask = SDL_HAT_DOWN;
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        mask = SDL_HAT_LEFT;
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        mask = SDL_HAT_RIGHT;
        break;
    default:
        break;
    }

    if (mask != 0 && rawHat(slot, mask))
        return true;

    return SDL_GameControllerGetButton(m_controllers[slot], b) != 0;
}

bool Gamepads::dpad(int slot, int direction) const
{
    static const SDL_GameControllerButton kButtons[4] = {
        SDL_CONTROLLER_BUTTON_DPAD_UP,
        SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    };

    if (direction < 0 || direction > 3)
        return false;
    return button(slot, kButtons[direction]);
}

Sint16 Gamepads::axis(int slot, SDL_GameControllerAxis a) const
{
    if (slot < 0 || slot >= kMaxControllers || !m_controllers[slot])
        return 0;
    return SDL_GameControllerGetAxis(m_controllers[slot], a);
}

std::string Gamepads::name(int slot) const
{
    if (slot < 0 || slot >= kMaxControllers || !m_controllers[slot])
        return std::string();
    const char* n = SDL_GameControllerName(m_controllers[slot]);
    return n ? std::string(n) : std::string();
}

std::string Gamepads::guid(int slot) const
{
    if (slot < 0 || slot >= kMaxControllers || !m_controllers[slot])
        return std::string();

    SDL_Joystick* joy = SDL_GameControllerGetJoystick(m_controllers[slot]);
    if (!joy)
        return std::string();

    char text[64];
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joy), text, sizeof(text));
    return std::string(text);
}

std::string Gamepads::mapping(int slot) const
{
    if (slot < 0 || slot >= kMaxControllers || !m_controllers[slot])
        return std::string();

    char* text = SDL_GameControllerMapping(m_controllers[slot]);
    if (!text)
        return std::string();

    std::string result(text);
    SDL_free(text);
    return result;
}

int Gamepads::buttonCount(int slot) const
{
    if (slot < 0 || slot >= kMaxControllers || !m_controllers[slot])
        return 0;
    SDL_Joystick* joy = SDL_GameControllerGetJoystick(m_controllers[slot]);
    return joy ? SDL_JoystickNumButtons(joy) : 0;
}

int Gamepads::axisCount(int slot) const
{
    if (slot < 0 || slot >= kMaxControllers || !m_controllers[slot])
        return 0;
    SDL_Joystick* joy = SDL_GameControllerGetJoystick(m_controllers[slot]);
    return joy ? SDL_JoystickNumAxes(joy) : 0;
}

int Gamepads::hatCount(int slot) const
{
    if (slot < 0 || slot >= kMaxControllers || !m_controllers[slot])
        return 0;
    SDL_Joystick* joy = SDL_GameControllerGetJoystick(m_controllers[slot]);
    return joy ? SDL_JoystickNumHats(joy) : 0;
}

bool Gamepads::rumble(int slot, Uint16 lowFrequency, Uint16 highFrequency,
                      Uint32 durationMs)
{
    if (slot < 0 || slot >= kMaxControllers || !m_controllers[slot])
        return false;
    return SDL_GameControllerRumble(m_controllers[slot], lowFrequency,
                                    highFrequency, durationMs) == 0;
}
} // namespace allnes::gamepad
