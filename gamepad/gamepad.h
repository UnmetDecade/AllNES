#ifndef ALLNES_GAMEPAD_GAMEPAD_H
#define ALLNES_GAMEPAD_GAMEPAD_H

#include <SDL.h>

#include <string>

namespace allnes::gamepad
{
// Owns up to two SDL game controllers with hot-plug support and a generic
// mapping fallback.
//
// SDL only exposes a device as a "game controller" (the abstract A/B/X/Y/DPAD
// interface) when its GUID has an entry in the built-in mapping database.
// Third-party pads -- GameSir, 8BitDo in X-input mode, PowerA, ... -- arrive
// as a bare joystick, so SDL_IsGameController() returns false and code that
// checks it silently ignores the pad. init()/handleEvent() add a generic
// Xbox-style mapping for such GUIDs so every pad can be driven through the
// same abstract interface as a first-party one.
class Gamepads
{
public:
    static constexpr int kMaxControllers = 2;

    Gamepads() = default;
    ~Gamepads();

    Gamepads(const Gamepads&)            = delete;
    Gamepads& operator=(const Gamepads&) = delete;

    // Initializes SDL_INIT_GAMECONTROLLER and opens every attached controller
    // (up to kMaxControllers). Returns false only when the subsystem fails.
    bool init();

    // Closes all controllers and shuts the subsystem down. Idempotent.
    void shutdown();

    // Hot-plug: call from the SDL event loop for every device event.
    void handleEvent(const SDL_Event& event);

    // Number of open controllers (0..kMaxControllers). Slot N maps to player
    // N + 1, so slot 0 = player 1, slot 1 = player 2.
    int count() const;

    // Abstract button/axis state of a slot; both are safe for an empty slot.
    bool   button(int slot, SDL_GameControllerButton b) const;
    Sint16 axis(int slot, SDL_GameControllerAxis a) const;

    // Human-readable controller name, or an empty string for an empty slot.
    std::string name(int slot) const;

    // Rumbles a slot. Returns false when the slot is empty or unsupported.
    bool rumble(int slot, Uint16 lowFrequency, Uint16 highFrequency,
                Uint32 durationMs);

private:
    void open(int deviceIndex);
    void closeSlot(int slot);
    int  findEmptySlot() const;

    SDL_GameController* m_controllers[kMaxControllers] = { nullptr, nullptr };
    SDL_JoystickID      m_instanceIds[kMaxControllers] = { 0, 0 };
    bool                m_initialized = false;
};
} // namespace allnes::gamepad

#endif // ALLNES_GAMEPAD_GAMEPAD_H
