#ifndef ALLNES_HOST_KEYBINDINGS_H
#define ALLNES_HOST_KEYBINDINGS_H

#include "port/IInputSource.h"

#include <SDL_scancode.h>

#include <array>
#include <string>

namespace host
{
// One SDL scancode per NES button, indexed by button bit position:
// A=0, B=1, Select=2, Start=3, Up=4, Down=5, Left=6, Right=7.
// (NesButton itself holds bit masks, not indices.)
using PlayerBindings = std::array<SDL_Scancode, allnes::ButtonCount>;

struct Keybindings
{
    std::array<PlayerBindings, 2> players;
};

Keybindings defaultKeybindings();

// Loads a keybindings.conf in the following format:
//   [Player1]
//   A = J
// Unknown keys/sections are ignored; problems are reported to stderr and the
// default binding for that button is kept.
bool loadKeybindings(const std::string& path, Keybindings& out);

// Maps a config key name (e.g. "RShift", "Numpad5") to an SDL scancode.
// Returns SDL_SCANCODE_UNKNOWN if the name is not recognised.
SDL_Scancode scancodeFromName(const std::string& name);

} // namespace host

#endif // ALLNES_HOST_KEYBINDINGS_H
