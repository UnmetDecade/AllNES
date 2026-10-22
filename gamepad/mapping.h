#ifndef ALLNES_GAMEPAD_MAPPING_H
#define ALLNES_GAMEPAD_MAPPING_H

#include <string>

namespace allnes::gamepad
{
// Physical capabilities reported by a joystick. Used to decide which D-pad
// binding a mapping can use.
struct MappingCounts
{
    int buttons = 0;
    int axes    = 0;
    int hats    = 0;
};

// Which of the four D-pad directions an SDL mapping string covers.
struct DpadCoverage
{
    bool up    = false;
    bool down  = false;
    bool left  = false;
    bool right = false;

    bool complete() const { return up && down && left && right; }
};

// Parses the dpup/dpdown/dpleft/dpright fields of an SDL mapping string.
DpadCoverage parseDpadCoverage(const std::string& mapping);

// Builds the generic Xbox-style fallback mapping used when SDL's database has
// no entry for a device. A hat D-pad is preferred; otherwise buttons 11..14
// (the macOS Xbox 360 layout) are used when the device exposes them.
std::string buildGenericMapping(const std::string& guid, MappingCounts counts);

// Returns a mapping with a complete D-pad when `mapping` only covers some
// directions. All other fields are preserved. Returns an empty string when
// nothing needs changing, the mapping cannot be parsed, or the device has
// neither a hat nor the Xbox 360 D-pad buttons.
std::string completeDpadMapping(const std::string& mapping, MappingCounts counts);
} // namespace allnes::gamepad

#endif // ALLNES_GAMEPAD_MAPPING_H
