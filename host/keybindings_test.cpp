// Small regression test for the keybindings parser. It also guards the
// bit-mask vs. array-index distinction: NesButton values are 1<<n, while the
// bindings array is indexed by n, and mixing them up corrupts the stack.

#include "keybindings.h"

#include <cstdio>
#include <string>

namespace
{
int failures = 0;

void expect(bool cond, const char* what)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}
} // namespace

int main(int argc, char* argv[])
{
    const std::string conf = argc > 1 ? argv[1] : "keybindings.conf";

    expect(host::scancodeFromName("J") == SDL_SCANCODE_J, "J maps to SDL_SCANCODE_J");
    expect(host::scancodeFromName("RShift") == SDL_SCANCODE_RSHIFT, "RShift maps to SDL_SCANCODE_RSHIFT");
    expect(host::scancodeFromName("Numpad5") == SDL_SCANCODE_KP_5, "Numpad5 maps to SDL_SCANCODE_KP_5");
    expect(host::scancodeFromName("nope") == SDL_SCANCODE_UNKNOWN, "unknown key name");

    host::Keybindings kb = host::defaultKeybindings();
    expect(kb.players[0][0] == SDL_SCANCODE_J, "default P1 A = J");
    expect(kb.players[0][1] == SDL_SCANCODE_K, "default P1 B = K");
    expect(kb.players[1][4] == SDL_SCANCODE_UP, "default P2 Up = Up");

    // The bundled config uses the same defaults, so parsing it must be a no-op.
    host::Keybindings parsed;
    const bool ok = host::loadKeybindings(conf, parsed);
    expect(ok, "bundled keybindings.conf parses cleanly");

    for (int player = 0; player < 2; ++player)
    {
        for (int button = 0; button < allnes::ButtonCount; ++button)
            expect(parsed.players[player][button] == kb.players[player][button],
                   "bundled config matches defaults");
    }

    std::printf("%s\n", failures == 0 ? "OK" : "FAILED");
    return failures == 0 ? 0 : 1;
}
