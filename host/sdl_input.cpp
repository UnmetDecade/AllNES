#include "sdl_input.h"

#include <cstdio>

namespace host
{
namespace
{
const int GamepadButtonCount = 8;
const SDL_GameControllerButton kGamepadMap[GamepadButtonCount] = {
    SDL_CONTROLLER_BUTTON_A,      // NES A
    SDL_CONTROLLER_BUTTON_X,      // NES B
    SDL_CONTROLLER_BUTTON_BACK,   // Select
    SDL_CONTROLLER_BUTTON_START,  // Start
    SDL_CONTROLLER_BUTTON_DPAD_UP,
    SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
};
} // namespace

SdlInput::SdlInput(const Keybindings& keys)
  : m_keys(keys)
{
}

SdlInput::~SdlInput()
{
    closeController();
    if (m_initialized)
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

bool SdlInput::init()
{
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
    {
        std::fprintf(stderr, "SDL game controller init failed: %s\n", SDL_GetError());
        return false;
    }
    m_initialized = true;

    for (int i = 0; i < SDL_NumJoysticks(); ++i)
    {
        if (SDL_IsGameController(i))
        {
            openController(i);
            break;
        }
    }

    return true;
}

void SdlInput::closeController()
{
    if (m_controller)
    {
        SDL_GameControllerClose(m_controller);
        m_controller = nullptr;
    }
}

void SdlInput::openController(int deviceIndex)
{
    closeController();
    m_controller = SDL_GameControllerOpen(deviceIndex);
    if (m_controller)
    {
        std::fprintf(stderr, "Game controller: %s\n", SDL_GameControllerName(m_controller));
    }
}

void SdlInput::poll()
{
    const Uint8* keyboard = SDL_GetKeyboardState(nullptr);

    for (int player = 0; player < 2; ++player)
    {
        std::uint8_t state = 0;
        for (int button = 0; button < allnes::ButtonCount; ++button)
        {
            const SDL_Scancode scancode = m_keys.players[player][button];
            if (scancode != SDL_SCANCODE_UNKNOWN && keyboard[scancode])
                state |= static_cast<std::uint8_t>(1u << button);
        }
        m_state[player] = state;
    }

    if (m_controller)
    {
        for (int button = 0; button < GamepadButtonCount; ++button)
        {
            if (SDL_GameControllerGetButton(m_controller, kGamepadMap[button]))
                m_state[0] |= static_cast<std::uint8_t>(1u << button);
        }
    }
}

std::uint8_t SdlInput::buttonState(int player) const
{
    if (player < 0 || player > 1)
        return 0;
    return m_state[player];
}

void SdlInput::handleEvent(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_CONTROLLERDEVICEADDED:
        if (!m_controller && SDL_IsGameController(event.cdevice.which))
            openController(event.cdevice.which);
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (m_controller &&
            event.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(m_controller)))
            closeController();
        break;
    default:
        break;
    }
}
} // namespace host
