#include "sdl_input.h"

namespace host
{
namespace
{
// SDL's abstract controller buttons -> NES buttons, in the order the NES shift
// register reports them (A, B, Select, Start, Up, Down, Left, Right). The same
// table drives both players.
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

SdlInput::~SdlInput() = default;

bool SdlInput::init()
{
    return m_pads.init();
}

void SdlInput::applyPad(int player, int slot)
{
    for (int button = 0; button < GamepadButtonCount; ++button)
    {
        if (m_pads.button(slot, kGamepadMap[button]))
            m_state[player] |= static_cast<std::uint8_t>(1u << button);
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

    // Slot 0 -> player 1, slot 1 -> player 2.
    for (int slot = 0; slot < allnes::gamepad::Gamepads::kMaxControllers; ++slot)
        applyPad(slot, slot);
}

std::uint8_t SdlInput::buttonState(int player) const
{
    if (player < 0 || player > 1)
        return 0;
    return m_state[player];
}

void SdlInput::handleEvent(const SDL_Event& event)
{
    m_pads.handleEvent(event);
}
} // namespace host
