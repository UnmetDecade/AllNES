#ifndef ALLNES_GAMEPAD_GAMEPAD_H
#define ALLNES_GAMEPAD_GAMEPAD_H

#include <SDL.h>

#include <string>

namespace allnes::gamepad
{
// Owns up to two SDL game controllers with hot-plug support and the community
// SDL_GameControllerDB mapping database.
//
// SDL only exposes a device as a "game controller" (the abstract A/B/X/Y/DPAD
// interface) when its GUID has an entry in the mapping database. init() loads
// the bundled database, then adds a generic Xbox-style mapping for GUIDs it
// still does not recognise. Finally, mappings that only describe some of the
// four D-pad directions are completed from the raw hat / button state so pads
// such as the macOS Xbox 360 controller report all four directions.
class Gamepads
{
public:
    static constexpr int kMaxControllers = 2;

    Gamepads() = default;
    ~Gamepads();

    Gamepads(const Gamepads&)            = delete;
    Gamepads& operator=(const Gamepads&) = delete;

    // Initializes SDL_INIT_GAMECONTROLLER, loads the mapping database and opens
    // every attached controller (up to kMaxControllers). Returns false only
    // when the subsystem fails.
    bool init();

    // Closes all controllers and shuts the subsystem down. Idempotent.
    void shutdown();

    // Hot-plug: call from the SDL event loop for every device event.
    void handleEvent(const SDL_Event& event);

    // Number of open controllers (0..kMaxControllers). Slot N maps to player
    // N + 1, so slot 0 = player 1, slot 1 = player 2.
    int count() const;

    // Abstract button/axis state of a slot; both are safe for an empty slot.
    // D-pad buttons also report the raw joystick hat, which keeps working when
    // the active mapping only covers some directions.
    bool   button(int slot, SDL_GameControllerButton b) const;
    Sint16 axis(int slot, SDL_GameControllerAxis a) const;

    // D-pad direction: 0 up, 1 down, 2 left, 3 right. Combines the mapped
    // button with the raw hat. Returns false for an invalid slot/direction.
    bool dpad(int slot, int direction) const;

    // Human-readable controller name, or an empty string for an empty slot.
    std::string name(int slot) const;

    // Diagnostics: GUID, active mapping string and raw device capabilities.
    std::string guid(int slot) const;
    std::string mapping(int slot) const;
    int         buttonCount(int slot) const;
    int         axisCount(int slot) const;
    int         hatCount(int slot) const;

    // Rumbles a slot. Returns false when the slot is empty or unsupported.
    bool rumble(int slot, Uint16 lowFrequency, Uint16 highFrequency,
                Uint32 durationMs);

private:
    // Registers a mapping for the device when needed and completes a partial
    // D-pad. Must run before SDL_GameControllerOpen().
    bool prepareDevice(int deviceIndex);
    void open(int deviceIndex);
    void closeSlot(int slot);
    int  findEmptySlot() const;
    bool rawHat(int slot, Uint8 mask) const;

    SDL_GameController* m_controllers[kMaxControllers] = { nullptr, nullptr };
    SDL_JoystickID      m_instanceIds[kMaxControllers] = { 0, 0 };
    bool                m_initialized = false;
};
} // namespace allnes::gamepad

#endif // ALLNES_GAMEPAD_GAMEPAD_H
