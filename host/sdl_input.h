#ifndef ALLNES_HOST_SDL_INPUT_H
#define ALLNES_HOST_SDL_INPUT_H

#include "keybindings.h"
#include "port/IInputSource.h"

#include "gamepad/gamepad.h"

#include <SDL.h>

namespace host
{
// Polls the keyboard and up to two game controllers. Each player is driven by
// the keyboard or their own controller (slot 0 -> player 1, slot 1 -> player 2);
// controller input is OR-ed onto the keyboard state.
class SdlInput : public allnes::IInputSource
{
public:
    explicit SdlInput(const Keybindings& keys);
    ~SdlInput() override;

    bool init();
    void poll();
    std::uint8_t buttonState(int player) const override;

    // Call from the host event loop for hot-plug support.
    void handleEvent(const SDL_Event& event);

    bool controllerConnected() const { return m_pads.count() > 0; }

private:
    void applyPad(int player, int slot);

    Keybindings              m_keys;
    std::uint8_t             m_state[2] = {0, 0};
    allnes::gamepad::Gamepads m_pads;
};
} // namespace host

#endif // ALLNES_HOST_SDL_INPUT_H
