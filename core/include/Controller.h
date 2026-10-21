#pragma once

#include "port/IInputSource.h"
#include <cstdint>

namespace allnes
{
using Byte = std::uint8_t;

// Models the NES controller's parallel-load shift register. Button bits come
// from the platform-independent input port, so this class has no knowledge of
// keyboards, SDL, or any other frontend.
class Controller
{
public:
    enum Buttons
    {
        A,
        B,
        Select,
        Start,
        Up,
        Down,
        Left,
        Right,
        TotalButtons,
    };

    void connect(const IInputSource* source, int player);

    void strobe(Byte value);
    Byte read();

private:
    const IInputSource* m_source = nullptr;
    int                 m_player = 0;

    bool         m_strobe = false;
    std::uint8_t m_shift  = 0;
};
} // namespace allnes
