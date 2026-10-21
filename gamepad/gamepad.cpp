#include "gamepad/gamepad.h"

#include <cstdio>
#include <string>

namespace allnes::gamepad
{
namespace
{
// Adds a generic Xbox-style mapping for the device's GUID when SDL's built-in
// database has no entry for it. Returns true when SDL now reports the device
// as a game controller.
bool ensureRecognized(int deviceIndex)
{
    if (SDL_IsGameController(deviceIndex))
        return true;

    SDL_Joystick* joy = SDL_JoystickOpen(deviceIndex);
    if (!joy)
        return false;

    const std::string name = SDL_JoystickName(joy) ? SDL_JoystickName(joy) : "unknown";
    char              guid[64];
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joy), guid, sizeof(guid));
    const int buttons = SDL_JoystickNumButtons(joy);
    const int axes    = SDL_JoystickNumAxes(joy);
    const int hats    = SDL_JoystickNumHats(joy);
    SDL_JoystickClose(joy);

    // Standard Xbox button/axis/hat order. Every field is added only when the
    // device actually reports that index, otherwise SDL rejects the mapping.
    std::string mapping = std::string(guid) + ",Generic Gamepad";
    const auto addButton = [&mapping, buttons](const char* field, int index) {
        if (buttons > index)
            mapping += std::string(",") + field + ":" + std::to_string(index);
    };
    addButton("a", 0);
    addButton("b", 1);
    addButton("x", 2);
    addButton("y", 3);
    addButton("leftshoulder", 4);
    addButton("rightshoulder", 5);
    addButton("back", 6);
    addButton("start", 7);
    addButton("leftstick", 8);
    addButton("rightstick", 9);
    addButton("guide", 10);

    const auto addAxis = [&mapping, axes](const char* field, int index) {
        if (axes > index)
            mapping += std::string(",") + field + ":" + std::to_string(index);
    };
    addAxis("leftx", 0);
    addAxis("lefty", 1);
    addAxis("rightx", 2);
    addAxis("righty", 3);
    addAxis("lefttrigger", 4);
    addAxis("righttrigger", 5);

    if (hats >= 1)
        mapping += ",dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8";

    const int added = SDL_GameControllerAddMapping(mapping.c_str());
    std::fprintf(stderr,
                 "gamepad: '%s' (buttons=%d axes=%d hats=%d) is not in SDL's "
                 "database; generic Xbox mapping %s\n",
                 name.c_str(), buttons, axes, hats,
                 added >= 0 ? "added" : "rejected");
    return SDL_IsGameController(deviceIndex);
}
} // namespace

Gamepads::~Gamepads()
{
    shutdown();
}

bool Gamepads::init()
{
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
    {
        std::fprintf(stderr, "gamepad: SDL game controller init failed: %s\n",
                     SDL_GetError());
        return false;
    }
    m_initialized = true;

    for (int i = 0; i < SDL_NumJoysticks(); ++i)
    {
        if (count() >= kMaxControllers)
            break;
        if (ensureRecognized(i))
            open(i);
    }
    return true;
}

void Gamepads::shutdown()
{
    for (int slot = 0; slot < kMaxControllers; ++slot)
        closeSlot(slot);

    if (m_initialized)
    {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        m_initialized = false;
    }
}

int Gamepads::findEmptySlot() const
{
    for (int slot = 0; slot < kMaxControllers; ++slot)
        if (!m_controllers[slot])
            return slot;
    return -1;
}

void Gamepads::open(int deviceIndex)
{
    const int slot = findEmptySlot();
    if (slot < 0)
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
}

void Gamepads::closeSlot(int slot)
{
    if (m_controllers[slot])
    {
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
    {
        // A device already in the database fires SDL_CONTROLLERDEVICEADDED as
        // well, so only the fallback path opens it here to avoid a double open.
        const int index = event.jdevice.which;
        if (!SDL_IsGameController(index) && count() < kMaxControllers &&
            ensureRecognized(index))
            open(index);
        break;
    }
    case SDL_CONTROLLERDEVICEADDED:
        if (count() < kMaxControllers)
            open(event.cdevice.which);
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

bool Gamepads::button(int slot, SDL_GameControllerButton b) const
{
    if (slot < 0 || slot >= kMaxControllers || !m_controllers[slot])
        return false;
    return SDL_GameControllerGetButton(m_controllers[slot], b) != 0;
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

bool Gamepads::rumble(int slot, Uint16 lowFrequency, Uint16 highFrequency,
                      Uint32 durationMs)
{
    if (slot < 0 || slot >= kMaxControllers || !m_controllers[slot])
        return false;
    return SDL_GameControllerRumble(m_controllers[slot], lowFrequency,
                                    highFrequency, durationMs) == 0;
}
} // namespace allnes::gamepad
