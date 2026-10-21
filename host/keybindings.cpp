#include "keybindings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

namespace host
{
namespace
{
std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

const std::map<std::string, SDL_Scancode>& scancodeTable()
{
    static const std::map<std::string, SDL_Scancode> table = {
        {"A", SDL_SCANCODE_A}, {"B", SDL_SCANCODE_B}, {"C", SDL_SCANCODE_C}, {"D", SDL_SCANCODE_D},
        {"E", SDL_SCANCODE_E}, {"F", SDL_SCANCODE_F}, {"G", SDL_SCANCODE_G}, {"H", SDL_SCANCODE_H},
        {"I", SDL_SCANCODE_I}, {"J", SDL_SCANCODE_J}, {"K", SDL_SCANCODE_K}, {"L", SDL_SCANCODE_L},
        {"M", SDL_SCANCODE_M}, {"N", SDL_SCANCODE_N}, {"O", SDL_SCANCODE_O}, {"P", SDL_SCANCODE_P},
        {"Q", SDL_SCANCODE_Q}, {"R", SDL_SCANCODE_R}, {"S", SDL_SCANCODE_S}, {"T", SDL_SCANCODE_T},
        {"U", SDL_SCANCODE_U}, {"V", SDL_SCANCODE_V}, {"W", SDL_SCANCODE_W}, {"X", SDL_SCANCODE_X},
        {"Y", SDL_SCANCODE_Y}, {"Z", SDL_SCANCODE_Z},

        {"Num0", SDL_SCANCODE_0}, {"Num1", SDL_SCANCODE_1}, {"Num2", SDL_SCANCODE_2},
        {"Num3", SDL_SCANCODE_3}, {"Num4", SDL_SCANCODE_4}, {"Num5", SDL_SCANCODE_5},
        {"Num6", SDL_SCANCODE_6}, {"Num7", SDL_SCANCODE_7}, {"Num8", SDL_SCANCODE_8},
        {"Num9", SDL_SCANCODE_9},

        {"Escape", SDL_SCANCODE_ESCAPE}, {"LControl", SDL_SCANCODE_LCTRL},
        {"LShift", SDL_SCANCODE_LSHIFT}, {"LAlt", SDL_SCANCODE_LALT},
        {"LSystem", SDL_SCANCODE_LGUI}, {"RControl", SDL_SCANCODE_RCTRL},
        {"RShift", SDL_SCANCODE_RSHIFT}, {"RAlt", SDL_SCANCODE_RALT},
        {"RSystem", SDL_SCANCODE_RGUI}, {"Menu", SDL_SCANCODE_MENU},
        {"LBracket", SDL_SCANCODE_LEFTBRACKET}, {"RBracket", SDL_SCANCODE_RIGHTBRACKET},
        {"SemiColon", SDL_SCANCODE_SEMICOLON}, {"Comma", SDL_SCANCODE_COMMA},
        {"Period", SDL_SCANCODE_PERIOD}, {"Quote", SDL_SCANCODE_APOSTROPHE},
        {"Slash", SDL_SCANCODE_SLASH}, {"BackSlash", SDL_SCANCODE_BACKSLASH},
        {"Tilde", SDL_SCANCODE_GRAVE}, {"Equal", SDL_SCANCODE_EQUALS},
        {"Dash", SDL_SCANCODE_MINUS}, {"Space", SDL_SCANCODE_SPACE},
        {"Return", SDL_SCANCODE_RETURN}, {"BackSpace", SDL_SCANCODE_BACKSPACE},
        {"Tab", SDL_SCANCODE_TAB}, {"PageUp", SDL_SCANCODE_PAGEUP},
        {"PageDown", SDL_SCANCODE_PAGEDOWN}, {"End", SDL_SCANCODE_END},
        {"Home", SDL_SCANCODE_HOME}, {"Insert", SDL_SCANCODE_INSERT},
        {"Delete", SDL_SCANCODE_DELETE},

        {"Add", SDL_SCANCODE_KP_PLUS}, {"Subtract", SDL_SCANCODE_KP_MINUS},
        {"Multiply", SDL_SCANCODE_KP_MULTIPLY}, {"Divide", SDL_SCANCODE_KP_DIVIDE},
        {"Decimal", SDL_SCANCODE_KP_PERIOD}, {"Enter", SDL_SCANCODE_KP_ENTER},

        {"Left", SDL_SCANCODE_LEFT}, {"Right", SDL_SCANCODE_RIGHT},
        {"Up", SDL_SCANCODE_UP}, {"Down", SDL_SCANCODE_DOWN},

        {"Numpad0", SDL_SCANCODE_KP_0}, {"Numpad1", SDL_SCANCODE_KP_1},
        {"Numpad2", SDL_SCANCODE_KP_2}, {"Numpad3", SDL_SCANCODE_KP_3},
        {"Numpad4", SDL_SCANCODE_KP_4}, {"Numpad5", SDL_SCANCODE_KP_5},
        {"Numpad6", SDL_SCANCODE_KP_6}, {"Numpad7", SDL_SCANCODE_KP_7},
        {"Numpad8", SDL_SCANCODE_KP_8}, {"Numpad9", SDL_SCANCODE_KP_9},

        {"F1", SDL_SCANCODE_F1}, {"F2", SDL_SCANCODE_F2}, {"F3", SDL_SCANCODE_F3},
        {"F4", SDL_SCANCODE_F4}, {"F5", SDL_SCANCODE_F5}, {"F6", SDL_SCANCODE_F6},
        {"F7", SDL_SCANCODE_F7}, {"F8", SDL_SCANCODE_F8}, {"F9", SDL_SCANCODE_F9},
        {"F10", SDL_SCANCODE_F10}, {"F11", SDL_SCANCODE_F11}, {"F12", SDL_SCANCODE_F12},
        {"F13", SDL_SCANCODE_F13}, {"F14", SDL_SCANCODE_F14}, {"F15", SDL_SCANCODE_F15},

        {"Pause", SDL_SCANCODE_PAUSE},
    };
    return table;
}

// NesButton values are bit masks (1<<0 .. 1<<7); the bindings array is indexed
// by bit position, so map names to 0..7.
bool buttonIndexFromName(const std::string& name, int& out)
{
    const std::string n = toLower(name);
    if (n == "a")           out = 0;
    else if (n == "b")      out = 1;
    else if (n == "select") out = 2;
    else if (n == "start")  out = 3;
    else if (n == "up")     out = 4;
    else if (n == "down")   out = 5;
    else if (n == "left")   out = 6;
    else if (n == "right")  out = 7;
    else return false;
    return true;
}
} // namespace

Keybindings defaultKeybindings()
{
    Keybindings kb;
    kb.players[0] = {SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_RETURN,
                     SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D};
    kb.players[1] = {SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9,
                     SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT};
    return kb;
}

SDL_Scancode scancodeFromName(const std::string& name)
{
    // The conf file is case-sensitive; accept exact matches first,
    // then a case-insensitive fallback for convenience.
    auto& table = scancodeTable();
    auto it = table.find(name);
    if (it != table.end())
        return it->second;

    for (auto& entry : table)
    {
        if (toLower(entry.first) == toLower(name))
            return entry.second;
    }
    return SDL_SCANCODE_UNKNOWN;
}

bool loadKeybindings(const std::string& path, Keybindings& out)
{
    out = defaultKeybindings();

    std::ifstream file(path);
    if (!file)
    {
        std::fprintf(stderr, "Could not open keybindings file '%s', using defaults.\n", path.c_str());
        return false;
    }

    int player = -1;
    std::string line;
    int lineNo = 0;
    bool ok = true;

    while (std::getline(file, line))
    {
        ++lineNo;
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        if (line[0] == '[')
        {
            const std::string section = toLower(trim(line.substr(1, line.find(']') - 1)));
            if (section == "player1")
                player = 0;
            else if (section == "player2")
                player = 1;
            else
                player = -1;
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        const std::string keyName   = trim(line.substr(0, eq));
        const std::string valueName = trim(line.substr(eq + 1));

        if (player < 0)
            continue; // key outside a known section

        int button;
        if (!buttonIndexFromName(keyName, button))
        {
            std::fprintf(stderr, "%s:%d: unknown button '%s'\n", path.c_str(), lineNo, keyName.c_str());
            continue;
        }

        const SDL_Scancode scancode = scancodeFromName(valueName);
        if (scancode == SDL_SCANCODE_UNKNOWN)
        {
            std::fprintf(stderr, "%s:%d: unknown key '%s'\n", path.c_str(), lineNo, valueName.c_str());
            ok = false;
            continue;
        }

        out.players[player][button] = scancode;
    }

    return ok;
}
} // namespace host
