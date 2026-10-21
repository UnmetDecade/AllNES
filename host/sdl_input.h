#ifndef ALLNES_HOST_SDL_INPUT_H
#define ALLNES_HOST_SDL_INPUT_H

#include "keybindings.h"
#include "port/IInputSource.h"

#include <SDL.h>

namespace host
{
// Polls the keyboard and (optionally) the first attached game controller.
// Player 1 can be driven by either; player 2 is keyboard-only.
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

    bool controllerConnected() const { return m_controller != nullptr; }

private:
    void closeController();
    void openController(int deviceIndex);

    Keybindings            m_keys;
    std::uint8_t           m_state[2] = {0, 0};
    SDL_GameController*    m_controller  = nullptr;
    bool                   m_initialized = false;
};
} // namespace host

#endif // ALLNES_HOST_SDL_INPUT_H
